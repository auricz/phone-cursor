package com.example.phonecursor

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

/**
 * Thin UDP client. Fire-and-forget by design: mouse/keyboard events are
 * latency-sensitive and occasionally dropping one is preferable to
 * retrying or blocking the caller.
 */
class NetworkClient {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var socket: DatagramSocket? = null
    private var address: InetAddress? = null
    private var port: Int = Config.DEFAULT_PORT

    val isConnected: Boolean
        get() = socket != null

    /** Resolves [host] and opens the socket. Suspends until the socket is ready or throws. */
    suspend fun connect(host: String, port: Int) = withContext(Dispatchers.IO) {
        val resolved = InetAddress.getByName(host)
        val newSocket = DatagramSocket()
        newSocket.soTimeout = Config.SOCKET_TIMEOUT_MS
        newSocket.connect(resolved, port)

        socket = newSocket
        address = resolved
        this@NetworkClient.port = port
    }

    fun disconnect() {
        socket?.close()
        socket = null
        address = null
    }

    /** Enqueues [payload] to be sent asynchronously; never blocks the caller. */
    fun send(payload: ByteArray) {
        val currentSocket = socket ?: return
        val currentAddress = address ?: return
        scope.launch {
            try {
                currentSocket.send(DatagramPacket(payload, payload.size, currentAddress, port))
            } catch (_: IOException) {
                // Best-effort delivery: dropping an occasional UDP packet is expected.
            }
        }
    }
}
