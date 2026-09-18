package com.example.phonecursor

/**
 * Owns the ECDH handshake and derived AES-256-GCM state for one
 * phone<->desktop session, from the phone's point of view (mirrors
 * desktop_server's SecureSession.h/.cpp; see the SecureEnvelope docs in
 * Protocol.kt for the wire format this produces/consumes).
 *
 * The phone always initiates with a HELLO and then waits: for a Local
 * connection the desktop confirms automatically, for an Internet
 * connection the operator must compare [confirmationCode] against the
 * desktop's console and accept it there first.
 */
class SecureSession {
    enum class Phase {
        AWAITING_HELLO_ACK,
        AWAITING_CONFIRM,
        CONFIRMED,
        REJECTED,
    }

    var phase: Phase = Phase.AWAITING_HELLO_ACK
        private set

    var confirmationCode: Int = 0
        private set

    private val localKeyPair = Crypto.EcdhKeyPair()
    private val localNonce = Crypto.randomBytes(Crypto.NONCE_SIZE)

    // phone -> desktop
    private lateinit var sendSalt: ByteArray
    private lateinit var sendKey: ByteArray
    private var sendCounter = 0L

    // desktop -> phone
    private lateinit var receiveSalt: ByteArray
    private lateinit var receiveKey: ByteArray
    private var highestReceivedCounter = -1L
    private var hasReceivedAny = false

    fun buildHello(): ByteArray = Protocol.hello(Protocol.TYPE_HELLO, localKeyPair.localPublicKey, localNonce)

    /** Consumes the desktop's HELLO_ACK: derives session key material and the confirmation code. */
    fun handleHelloAck(helloAck: Protocol.HelloPacket) {
        val sharedSecret = localKeyPair.agreeWith(helloAck.publicKey)
        val hkdfSalt = localNonce + helloAck.nonce // phoneNonce || desktopNonce, matches desktop's derivation

        val codeBytes = Crypto.hkdf(sharedSecret, hkdfSalt, Config.HKDF_INFO_CONFIRMATION_CODE, 4)
        val codeValue = ((codeBytes[0].toInt() and 0xFF).toLong() shl 24) or
            ((codeBytes[1].toInt() and 0xFF).toLong() shl 16) or
            ((codeBytes[2].toInt() and 0xFF).toLong() shl 8) or
            (codeBytes[3].toInt() and 0xFF).toLong()
        var modulus = 1L
        repeat(Config.CONFIRMATION_CODE_DIGITS) { modulus *= 10 }
        confirmationCode = (codeValue % modulus).toInt()

        val (sSalt, sKey) = deriveDirectionKey(sharedSecret, hkdfSalt, Config.HKDF_INFO_PHONE_TO_DESKTOP_KEY)
        sendSalt = sSalt
        sendKey = sKey

        val (rSalt, rKey) = deriveDirectionKey(sharedSecret, hkdfSalt, Config.HKDF_INFO_DESKTOP_TO_PHONE_KEY)
        receiveSalt = rSalt
        receiveKey = rKey

        phase = Phase.AWAITING_CONFIRM
    }

    fun markConfirmed() {
        phase = Phase.CONFIRMED
    }

    fun markRejected() {
        phase = Phase.REJECTED
    }

    /** Wraps an outgoing phone->desktop packet ([Protocol]'s builders' output: type byte + payload)
     * into a SecureEnvelope. */
    fun encrypt(fullPacket: ByteArray): ByteArray {
        val type = fullPacket[0]
        val payload = fullPacket.copyOfRange(1, fullPacket.size)
        val counter = sendCounter++
        val nonce = buildNonce(sendSalt, counter)

        return if (Protocol.requiresConfidentiality(type)) {
            val sealed = Crypto.seal(sendKey, nonce, byteArrayOf(type), payload)
            Protocol.buildSecureEnvelope(type, counter, sealed.tag, sealed.ciphertext)
        } else {
            val sealed = Crypto.seal(sendKey, nonce, byteArrayOf(type) + payload, ByteArray(0))
            Protocol.buildSecureEnvelope(type, counter, sealed.tag, payload)
        }
    }

    /** Unwraps an incoming SecureEnvelope from the desktop. Returns the reconstructed packet
     * bytes (type byte + payload), or null if malformed, unauthenticated, or a replay. */
    fun decrypt(data: ByteArray): ByteArray? {
        val envelope = Protocol.parseSecureEnvelope(data) ?: return null
        if (hasReceivedAny && envelope.counter <= highestReceivedCounter) return null // replay

        val nonce = buildNonce(receiveSalt, envelope.counter)
        val plaintext = if (Protocol.requiresConfidentiality(envelope.innerType)) {
            Crypto.open(receiveKey, nonce, byteArrayOf(envelope.innerType), envelope.body, envelope.tag)
        } else {
            val aad = byteArrayOf(envelope.innerType) + envelope.body
            Crypto.open(receiveKey, nonce, aad, ByteArray(0), envelope.tag)?.let { envelope.body }
        } ?: return null

        highestReceivedCounter = envelope.counter
        hasReceivedAny = true
        return byteArrayOf(envelope.innerType) + plaintext
    }

    private fun deriveDirectionKey(sharedSecret: ByteArray, hkdfSalt: ByteArray, info: String): Pair<ByteArray, ByteArray> {
        val material = Crypto.hkdf(sharedSecret, hkdfSalt, info, Crypto.AEAD_SALT_SIZE + Crypto.AEAD_KEY_SIZE)
        val salt = material.copyOfRange(0, Crypto.AEAD_SALT_SIZE)
        val key = material.copyOfRange(Crypto.AEAD_SALT_SIZE, material.size)
        return salt to key
    }

    private fun buildNonce(salt: ByteArray, counter: Long): ByteArray {
        val nonce = ByteArray(Crypto.AEAD_NONCE_SIZE)
        salt.copyInto(nonce, 0)
        for (i in 0 until 8) {
            nonce[salt.size + i] = ((counter shr (8 * (7 - i))) and 0xFF).toByte()
        }
        return nonce
    }
}
