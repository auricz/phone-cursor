package com.example.phonecursor

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import kotlin.time.Duration.Companion.milliseconds

/**
 * UDP client plus the secure handshake state machine (see SecureSession).
 * Outgoing packets are fire-and-forget by design: mouse/keyboard events are
 * latency-sensitive and occasionally dropping one is preferable to
 * retrying or blocking the caller. Incoming packets (handshake replies)
 * are read on a background receive loop, since the desktop now talks back.
 */
class NetworkClient {

    sealed class HandshakeState {
        data object Connecting : HandshakeState()
        data class AwaitingConfirmation(val code: Int) : HandshakeState()
        data object Confirmed : HandshakeState()
        data class Rejected(val reason: String) : HandshakeState()
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var socket: DatagramSocket? = null
    private var address: InetAddress? = null
    private var port: Int = Config.DEFAULT_PORT
    private var session: SecureSession? = null
    private var receiveJob: Job? = null

    private val _state = MutableStateFlow<HandshakeState>(HandshakeState.Connecting)
    val state: StateFlow<HandshakeState> = _state

    val isConnected: Boolean
        get() = socket != null

    /** Connects to a desktop on the same local network at [host]:[port]. */
    suspend fun connectLocal(host: String, port: Int) = withContext(Dispatchers.IO) {
        val resolved = InetAddress.getByName(host)
        val newSocket = DatagramSocket()
        newSocket.connect(resolved, port)

        socket = newSocket
        address = resolved
        this@NetworkClient.port = port
        startHandshake()
    }

    /**
     * Connects to a desktop over the internet using [pairingCode]: discovers
     * this device's public address via STUN, exchanges it with the desktop
     * through the rendezvous server, then punches through NATs before
     * starting the same handshake as connectLocal.
     */
    suspend fun connectInternet(pairingCode: String) = withContext(Dispatchers.IO) {
        val newSocket = DatagramSocket()
        socket = newSocket

        val publicAdder = StunClient.discoverPublicAddress(
            newSocket, Config.STUN_SERVER_HOST, Config.STUN_SERVER_PORT, Config.STUN_TIMEOUT_MS,
        ) ?: throw IOException("Could not reach the STUN server - check your internet connection")

        val rendezvous = RendezvousClient(Config.RENDEZVOUS_HOST, Config.RENDEZVOUS_USE_TLS)
        rendezvous.connect(pairingCode, "phone")
        rendezvous.sendCandidate("${publicAdder.ip}:${publicAdder.port}")
        val peerCandidateText = rendezvous.receiveCandidate(Config.INTERNET_HANDSHAKE_TIMEOUT_MS)
            ?: throw IOException("Timed out waiting for the desktop - check the pairing code and try again")
        rendezvous.close()

        val peerEndpoint = parseEndpoint(peerCandidateText)
            ?: throw IOException("Rendezvous server returned an invalid address")

        val resolved = InetAddress.getByName(peerEndpoint.first)
        newSocket.connect(resolved, peerEndpoint.second)
        address = resolved
        port = peerEndpoint.second

        // A handful of unsolicited datagrams opens this device's NAT mapping
        // toward the desktop, which does the same toward us at the same
        // time - once both mappings are open, packets flow directly with no
        // relay. Content is irrelevant: the desktop safely ignores anything
        // that doesn't parse as a HELLO or a valid secure envelope.
        repeat(5) {
            try {
                newSocket.send(DatagramPacket(byteArrayOf(0), 1, resolved, peerEndpoint.second))
            } catch (_: IOException) {
                // Ignored: hole punching is best-effort.
            }
            delay(200.milliseconds)
        }

        startHandshake()
    }

    private fun parseEndpoint(text: String): Pair<String, Int>? {
        val colon = text.lastIndexOf(':')
        if (colon == -1) return null
        val port = text.substring(colon + 1).toIntOrNull() ?: return null
        if (port !in 1..65535) return null
        return text.substring(0, colon) to port
    }

    private fun startHandshake() {
        val newSession = SecureSession()
        session = newSession
        _state.value = HandshakeState.Connecting
        startReceiveLoop()
        sendRaw(newSession.buildHello())
    }

    private fun startReceiveLoop() {
        val currentSocket = socket ?: return
        receiveJob = scope.launch {
            val buffer = ByteArray(1024)
            while (isActive) {
                val packet = DatagramPacket(buffer, buffer.size)
                try {
                    currentSocket.receive(packet)
                } catch (_: IOException) {
                    break // socket closed
                }
                handleIncoming(packet.data.copyOfRange(0, packet.length))
            }
        }
    }

    private fun handleIncoming(data: ByteArray) {
        val currentSession = session ?: return
        if (data.isNotEmpty() && data[0] == Protocol.TYPE_HELLO_ACK) {
            val helloAck = Protocol.parseHello(data) ?: return
            currentSession.handleHelloAck(helloAck)
            _state.value = HandshakeState.AwaitingConfirmation(currentSession.confirmationCode)
            return
        }

        val packetBytes = currentSession.decrypt(data) ?: return
        when (packetBytes[0]) {
            Protocol.TYPE_HANDSHAKE_CONFIRM -> {
                currentSession.markConfirmed()
                _state.value = HandshakeState.Confirmed
            }
            Protocol.TYPE_HANDSHAKE_REJECT -> {
                currentSession.markRejected()
                _state.value = HandshakeState.Rejected("The desktop declined the connection")
            }
        }
    }

    fun disconnect() {
        receiveJob?.cancel()
        socket?.close()
        socket = null
        address = null
        session = null
    }

    /** Encrypts and enqueues [payload] (a full Protocol packet: type byte + body) to be sent
     * asynchronously under the current session; never blocks the caller. Dropped if the
     * handshake hasn't been confirmed yet. */
    fun send(payload: ByteArray) {
        val currentSession = session ?: return
        if (currentSession.phase != SecureSession.Phase.CONFIRMED) return
        sendRaw(currentSession.encrypt(payload))
    }

    private fun sendRaw(bytes: ByteArray) {
        val currentSocket = socket ?: return
        val currentAddress = address ?: return
        scope.launch {
            try {
                currentSocket.send(DatagramPacket(bytes, bytes.size, currentAddress, port))
            } catch (_: IOException) {
                // Best-effort delivery: dropping an occasional UDP packet is expected.
            }
        }
    }
}
