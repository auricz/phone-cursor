package com.example.phonecursor

import java.math.BigInteger
import java.security.AlgorithmParameters
import java.security.GeneralSecurityException
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.SecureRandom
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECPoint
import java.security.spec.ECPublicKeySpec
import java.security.interfaces.ECPublicKey
import javax.crypto.Cipher
import javax.crypto.KeyAgreement
import javax.crypto.Mac
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * Cryptographic primitives backing the phone<->desktop secure session (see
 * SecureSession). Implemented on top of the standard JCA/JCE providers
 * bundled with Android, so no third-party crypto library is needed.
 *
 * Wire format note: ECDH public keys are exchanged as a standard SEC1
 * uncompressed point (0x04 || X || Y, 65 bytes for P-256), matching
 * desktop_server's Crypto.h.
 */
object Crypto {
    const val PUBLIC_KEY_SIZE = 65
    const val NONCE_SIZE = 16
    const val AEAD_KEY_SIZE = 32
    const val AEAD_SALT_SIZE = 4
    const val AEAD_NONCE_SIZE = 12 // salt(4) || counter(8)
    const val AEAD_TAG_SIZE = 16

    private const val CURVE_NAME = "secp256r1"
    private const val COORDINATE_SIZE = (PUBLIC_KEY_SIZE - 1) / 2 // 32 bytes for P-256

    private val curveParams: ECParameterSpec by lazy {
        val params = AlgorithmParameters.getInstance("EC")
        params.init(ECGenParameterSpec(CURVE_NAME))
        params.getParameterSpec(ECParameterSpec::class.java)
    }

    fun randomBytes(length: Int): ByteArray {
        val bytes = ByteArray(length)
        SecureRandom().nextBytes(bytes)
        return bytes
    }

    private fun BigInteger.toFixedLengthByteArray(length: Int): ByteArray {
        val raw = toByteArray()
        return when {
            raw.size == length -> raw
            raw.size == length + 1 && raw[0] == 0.toByte() -> raw.copyOfRange(1, raw.size)
            raw.size < length -> ByteArray(length - raw.size) + raw
            else -> throw IllegalStateException("EC coordinate is larger than $length bytes")
        }
    }

    private fun encodePublicKey(publicKey: ECPublicKey): ByteArray {
        val x = publicKey.w.affineX.toFixedLengthByteArray(COORDINATE_SIZE)
        val y = publicKey.w.affineY.toFixedLengthByteArray(COORDINATE_SIZE)
        return byteArrayOf(0x04) + x + y
    }

    private fun decodePublicKey(wireKey: ByteArray): java.security.PublicKey {
        require(wireKey.size == PUBLIC_KEY_SIZE) { "public key must be $PUBLIC_KEY_SIZE bytes" }
        val x = BigInteger(1, wireKey.copyOfRange(1, 1 + COORDINATE_SIZE))
        val y = BigInteger(1, wireKey.copyOfRange(1 + COORDINATE_SIZE, PUBLIC_KEY_SIZE))
        val point = ECPoint(x, y)
        val keyFactory = KeyFactory.getInstance("EC")
        return keyFactory.generatePublic(ECPublicKeySpec(point, curveParams))
    }

    /** An ephemeral P-256 ECDH key pair, valid for one handshake. */
    class EcdhKeyPair {
        private val keyPair = run {
            val generator = KeyPairGenerator.getInstance("EC")
            generator.initialize(ECGenParameterSpec(CURVE_NAME))
            generator.generateKeyPair()
        }

        val localPublicKey: ByteArray = encodePublicKey(keyPair.public as ECPublicKey)

        /** Computes the raw ECDH shared secret (big-endian X coordinate) with a peer's public key. */
        fun agreeWith(peerPublicKey: ByteArray): ByteArray {
            val peerKey = decodePublicKey(peerPublicKey)
            val keyAgreement = KeyAgreement.getInstance("ECDH")
            keyAgreement.init(keyPair.private)
            keyAgreement.doPhase(peerKey, true)
            return keyAgreement.generateSecret()
        }
    }

    private fun hmacSha256(key: ByteArray, data: ByteArray): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal(data)
    }

    /** HKDF-SHA256 (RFC 5869): Extract-then-Expand, truncated/expanded to outputLen bytes. */
    fun hkdf(secret: ByteArray, salt: ByteArray, info: String, outputLen: Int): ByteArray {
        val prk = hmacSha256(salt, secret)
        val infoBytes = info.toByteArray(Charsets.UTF_8)

        val okm = ByteArray(outputLen)
        var previousBlock = ByteArray(0)
        var written = 0
        var counter = 1
        while (written < outputLen) {
            val block = hmacSha256(prk, previousBlock + infoBytes + byteArrayOf(counter.toByte()))
            val toCopy = minOf(block.size, outputLen - written)
            block.copyInto(okm, written, 0, toCopy)
            written += toCopy
            previousBlock = block
            counter++
        }
        return okm
    }

    data class SealedPacket(val tag: ByteArray, val ciphertext: ByteArray) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false

            other as SealedPacket

            if (!tag.contentEquals(other.tag)) return false
            if (!ciphertext.contentEquals(other.ciphertext)) return false

            return true
        }

        override fun hashCode(): Int {
            var result = tag.contentHashCode()
            result = 31 * result + ciphertext.contentHashCode()
            return result
        }
    }

    /** AES-256-GCM seal. Pass an empty plaintext (with the data instead placed in aad) to
     * authenticate without encrypting - used for packet types that only need integrity. */
    fun seal(key: ByteArray, nonce: ByteArray, aad: ByteArray, plaintext: ByteArray): SealedPacket {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(AEAD_TAG_SIZE * 8, nonce))
        cipher.updateAAD(aad)
        val output = cipher.doFinal(plaintext) // ciphertext || tag
        val ciphertext = output.copyOfRange(0, output.size - AEAD_TAG_SIZE)
        val tag = output.copyOfRange(output.size - AEAD_TAG_SIZE, output.size)
        return SealedPacket(tag, ciphertext)
    }

    /** AES-256-GCM open. Returns null if the tag doesn't verify. */
    fun open(key: ByteArray, nonce: ByteArray, aad: ByteArray, ciphertext: ByteArray, tag: ByteArray): ByteArray? {
        return try {
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(AEAD_TAG_SIZE * 8, nonce))
            cipher.updateAAD(aad)
            cipher.doFinal(ciphertext + tag)
        } catch (e: GeneralSecurityException) {
            null
        }
    }
}
