package com.example.phonecursor

import java.io.IOException
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener

/**
 * Talks to the Cloudflare Worker rendezvous server (see rendezvous_server/)
 * over plain HTTP (to allocate a pairing code) and WebSocket (to exchange
 * each side's STUN-discovered candidate address once paired). Mirrors
 * desktop_server's RendezvousClient.h/.cpp.
 */
class RendezvousClient(private val host: String, private val useTls: Boolean) {
    private val client = OkHttpClient()
    private val incoming = LinkedBlockingQueue<String>()
    private var webSocket: WebSocket? = null
    private val wsScheme get() = if (useTls) "wss" else "ws"

    /**
     * Opens the WebSocket for an existing pairing code under this device's
     * role ("desktop" or "phone"). Throws with a descriptive message if the
     * code is unknown/expired or that role is already connected.
     */
    suspend fun connect(pairingCode: String, role: String): Unit = suspendCancellableCoroutine { continuation ->
        val request = Request.Builder().url("$wsScheme://$host/session/$pairingCode/ws?role=$role").build()
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                if (continuation.isActive) continuation.resume(Unit)
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                incoming.offer(text)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (continuation.isActive) {
                    continuation.resumeWithException(IOException(describeFailure(response, t)))
                }
            }
        })
    }

    /** Sends this device's candidate address as "ip:port" text. */
    fun sendCandidate(ipPort: String) {
        webSocket?.send(ipPort)
    }

    /** Waits up to [timeoutMs] for the peer's candidate address ("ip:port").
     * Returns null on timeout or if the connection closes first. */
    suspend fun receiveCandidate(timeoutMs: Long): String? = withContext(Dispatchers.IO) {
        incoming.poll(timeoutMs, TimeUnit.MILLISECONDS)
    }

    fun close() {
        webSocket?.close(1000, null)
        webSocket = null
    }

    private fun describeFailure(response: Response?, t: Throwable): String = when (response?.code) {
        404 -> "Pairing code not found or expired"
        409 -> "That role is already connected with this pairing code"
        else -> t.message ?: "WebSocket connection failed"
    }
}
