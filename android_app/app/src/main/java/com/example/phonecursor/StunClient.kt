package com.example.phonecursor

import java.io.IOException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress

/**
 * Minimal STUN (RFC 5389) binding-request client, used only in Internet
 * mode. Cloudflare Workers (the rendezvous server's runtime) can't open raw
 * UDP sockets, so it can't itself observe this device's NAT-mapped public
 * address - that's exactly what STUN is for, so the phone asks a standard
 * public STUN server directly instead. The rendezvous server only relays
 * the resulting address between the two devices (see RendezvousClient).
 * Mirrors desktop_server's StunClient.h/.cpp.
 */
object StunClient {
    data class Endpoint(val ip: String, val port: Int)

    private const val MAGIC_COOKIE = 0x2112A442L
    private const val BINDING_REQUEST = 0x0001
    private const val BINDING_SUCCESS_RESPONSE = 0x0101
    private const val ATTR_MAPPED_ADDRESS = 0x0001
    private const val ATTR_XOR_MAPPED_ADDRESS = 0x0020
    private const val HEADER_SIZE = 20
    private const val TRANSACTION_ID_SIZE = 12

    /**
     * Sends a STUN binding request on [socket] (an already-bound UDP socket)
     * and returns this device's server-reflexive (public) address as seen by
     * the STUN server. Must be called on the same socket that will later be
     * used for the peer-to-peer session, since that's the NAT mapping that
     * actually matters for hole punching. Returns null on timeout or a
     * malformed response.
     */
    fun discoverPublicAddress(socket: DatagramSocket, stunHost: String, stunPort: Int, timeoutMs: Int): Endpoint? {
        val transactionId = Crypto.randomBytes(TRANSACTION_ID_SIZE)
        val request = buildBindingRequest(transactionId)

        val originalTimeout = socket.soTimeout
        return try {
            socket.soTimeout = timeoutMs
            // IPv4 only, like desktop_server's StunClient: the parser and the
            // "ip:port" candidates exchanged with the desktop are IPv4-only, and
            // getByName may otherwise pick the server's IPv6 address.
            val address = InetAddress.getAllByName(stunHost).firstOrNull { it is Inet4Address } ?: return null
            socket.send(DatagramPacket(request, request.size, address, stunPort))

            val buffer = ByteArray(512)
            val responsePacket = DatagramPacket(buffer, buffer.size)
            socket.receive(responsePacket)
            parseBindingResponse(buffer, responsePacket.length, transactionId)
        } catch (e: IOException) {
            null
        } finally {
            socket.soTimeout = originalTimeout
        }
    }

    private fun buildBindingRequest(transactionId: ByteArray): ByteArray {
        val message = ByteArray(HEADER_SIZE)
        writeU16(message, 0, BINDING_REQUEST)
        writeU16(message, 2, 0) // no attributes
        writeU32(message, 4, MAGIC_COOKIE)
        transactionId.copyInto(message, 8)
        return message
    }

    private fun parseBindingResponse(data: ByteArray, length: Int, expectedTransactionId: ByteArray): Endpoint? {
        if (length < HEADER_SIZE) return null
        if (readU16(data, 0) != BINDING_SUCCESS_RESPONSE) return null
        if (readU32(data, 4) != MAGIC_COOKIE) return null
        if (!data.copyOfRange(8, 20).contentEquals(expectedTransactionId)) return null

        val attrsLength = readU16(data, 2)
        var offset = HEADER_SIZE
        val end = minOf(HEADER_SIZE + attrsLength, length)

        var fallback: Endpoint? = null
        while (offset + 4 <= end) {
            val attrType = readU16(data, offset)
            val attrLen = readU16(data, offset + 2)
            if (offset + 4 + attrLen > end) break

            if (attrType == ATTR_XOR_MAPPED_ADDRESS) {
                parseMappedAddress(data, offset + 4, attrLen, isXor = true)?.let { return it }
            } else if (attrType == ATTR_MAPPED_ADDRESS && fallback == null) {
                fallback = parseMappedAddress(data, offset + 4, attrLen, isXor = false)
            }

            offset += 4 + ((attrLen + 3) and 3.inv())
        }
        return fallback
    }

    private fun parseMappedAddress(data: ByteArray, offset: Int, length: Int, isXor: Boolean): Endpoint? {
        if (length < 8 || data[offset + 1] != 0x01.toByte()) return null // family must be IPv4

        var port = readU16(data, offset + 2)
        var addr = readU32(data, offset + 4)
        if (isXor) {
            port = port xor (MAGIC_COOKIE ushr 16).toInt()
            addr = addr xor MAGIC_COOKIE
        }

        val ipBytes = byteArrayOf(
            ((addr shr 24) and 0xFF).toByte(),
            ((addr shr 16) and 0xFF).toByte(),
            ((addr shr 8) and 0xFF).toByte(),
            (addr and 0xFF).toByte(),
        )
        val ip = InetAddress.getByAddress(ipBytes).hostAddress ?: return null
        return Endpoint(ip, port)
    }

    private fun writeU16(buffer: ByteArray, offset: Int, value: Int) {
        buffer[offset] = ((value shr 8) and 0xFF).toByte()
        buffer[offset + 1] = (value and 0xFF).toByte()
    }

    private fun writeU32(buffer: ByteArray, offset: Int, value: Long) {
        buffer[offset] = ((value shr 24) and 0xFF).toByte()
        buffer[offset + 1] = ((value shr 16) and 0xFF).toByte()
        buffer[offset + 2] = ((value shr 8) and 0xFF).toByte()
        buffer[offset + 3] = (value and 0xFF).toByte()
    }

    private fun readU16(data: ByteArray, offset: Int): Int =
        ((data[offset].toInt() and 0xFF) shl 8) or (data[offset + 1].toInt() and 0xFF)

    private fun readU32(data: ByteArray, offset: Int): Long =
        (((data[offset].toLong() and 0xFF) shl 24) or ((data[offset + 1].toLong() and 0xFF) shl 16) or
            ((data[offset + 2].toLong() and 0xFF) shl 8) or (data[offset + 3].toLong() and 0xFF))
}
