package com.example.phonecursor

import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Wire format shared with desktop_server's Protocol.h. Every packet is a
 * small, fixed-layout, little-endian UDP datagram:
 *
 *   SENSOR    : u8 type=1, float32 qw, qx, qy, qz (device orientation,
 *               game rotation vector)                            (17 bytes)
 *   CLICK     : u8 type=2, u8 button (0=left, 1=right)            (2 bytes)
 *   KEY       : u8 type=3, u8 action (0=char, 1=backspace),
 *               u16 char (UTF-16 code unit, only used when action=0)
 *                                                                  (4 bytes)
 *   CALIBRATE : u8 type=4, float32 qw, qx, qy, qz (device
 *               orientation to treat as the reference pose)      (17 bytes)
 *   CONFIG    : u8 type=5, u8 option (0=yawMaxPercent,
 *               1=pitchMaxPercent), float32 value                 (6 bytes)
 *
 * The desktop does all world-frame projection and angle-to-cursor mapping
 * (see desktop_server's MotionProcessor); the phone only relays its raw
 * orientation, adjusted for the user's invert preferences (see
 * MotionTracker) and the max-angle sliders (see MainActivity). Keep both
 * sides of this format in sync when changing it.
 *
 * SENSOR/CLICK/KEY/CALIBRATE/CONFIG packets are never sent on the wire in
 * the layout above once a session is established - they're wrapped in a
 * SecureEnvelope (see below and SecureSession) for integrity, and KEY
 * packets additionally for confidentiality, per this project's security
 * requirements. The layout above is what's inside the envelope.
 *
 * Two more packet types bootstrap that envelope's keys:
 *
 *   HELLO     : u8 type=6, 65-byte ECDH public key (SEC1 uncompressed
 *               point: 0x04 || X || Y), 16-byte random nonce   (82 bytes)
 *   HELLO_ACK : u8 type=7, same layout as HELLO, desktop's key/nonce
 *                                                                (82 bytes)
 *   HANDSHAKE_CONFIRM : u8 type=8, sent (via SecureEnvelope, empty body)
 *               once the desktop operator accepts the confirmation code.
 *   HANDSHAKE_REJECT  : u8 type=9, sent (via SecureEnvelope, empty body)
 *               if the operator declines it instead.
 *
 * SecureEnvelope: u8 innerType, u64 counter (big-endian, unique per
 * direction, monotonically increasing), 16-byte AES-256-GCM tag, then the
 * body - ciphertext for KEY, plaintext (also covered by the tag as
 * associated data) for everything else.
 */
object Protocol {

    private const val TYPE_SENSOR: Byte = 1
    private const val TYPE_CLICK: Byte = 2
    private const val TYPE_KEY: Byte = 3
    private const val TYPE_CALIBRATE: Byte = 4
    private const val TYPE_CONFIG: Byte = 5
    const val TYPE_HELLO: Byte = 6
    const val TYPE_HELLO_ACK: Byte = 7
    const val TYPE_HANDSHAKE_CONFIRM: Byte = 8
    const val TYPE_HANDSHAKE_REJECT: Byte = 9

    const val BUTTON_LEFT: Byte = 0
    const val BUTTON_RIGHT: Byte = 1

    const val KEY_ACTION_CHAR: Byte = 0
    const val KEY_ACTION_BACKSPACE: Byte = 1

    const val CONFIG_OPT_YAW_MAX_PERCENT: Byte = 0
    const val CONFIG_OPT_PITCH_MAX_PERCENT: Byte = 1

    const val PUBLIC_KEY_SIZE = 65
    const val HANDSHAKE_NONCE_SIZE = 16
    const val HELLO_PACKET_SIZE = 1 + PUBLIC_KEY_SIZE + HANDSHAKE_NONCE_SIZE
    const val SECURE_ENVELOPE_TAG_SIZE = 16
    const val SECURE_ENVELOPE_HEADER_SIZE = 1 + 8 + SECURE_ENVELOPE_TAG_SIZE

    /** Returns true if a packet type's body must stay confidential (encrypted)
     * once wrapped in a SecureEnvelope, not just authenticated. */
    fun requiresConfidentiality(type: Byte): Boolean = type == TYPE_KEY

    data class HelloPacket(val publicKey: ByteArray, val nonce: ByteArray)

    data class SecureEnvelope(val innerType: Byte, val counter: Long, val tag: ByteArray, val body: ByteArray)

    fun sensor(qw: Float, qx: Float, qy: Float, qz: Float): ByteArray =
        ByteBuffer.allocate(17)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(TYPE_SENSOR)
            .putFloat(qw)
            .putFloat(qx)
            .putFloat(qy)
            .putFloat(qz)
            .array()

    fun click(button: Byte): ByteArray =
        ByteBuffer.allocate(2)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(TYPE_CLICK)
            .put(button)
            .array()

    fun keyChar(char: Char): ByteArray =
        ByteBuffer.allocate(4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(TYPE_KEY)
            .put(KEY_ACTION_CHAR)
            .putShort(char.code.toShort())
            .array()

    fun keyBackspace(): ByteArray =
        ByteBuffer.allocate(4)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(TYPE_KEY)
            .put(KEY_ACTION_BACKSPACE)
            .putShort(0)
            .array()

    fun calibrate(qw: Float, qx: Float, qy: Float, qz: Float): ByteArray =
        ByteBuffer.allocate(17)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(TYPE_CALIBRATE)
            .putFloat(qw)
            .putFloat(qx)
            .putFloat(qy)
            .putFloat(qz)
            .array()

    fun config(option: Byte, value: Float): ByteArray =
        ByteBuffer.allocate(6)
            .order(ByteOrder.LITTLE_ENDIAN)
            .put(TYPE_CONFIG)
            .put(option)
            .putFloat(value)
            .array()

    /** Shared layout for HELLO and HELLO_ACK; [type] distinguishes them. */
    fun hello(type: Byte, publicKey: ByteArray, nonce: ByteArray): ByteArray {
        require(publicKey.size == PUBLIC_KEY_SIZE) { "public key must be $PUBLIC_KEY_SIZE bytes" }
        require(nonce.size == HANDSHAKE_NONCE_SIZE) { "nonce must be $HANDSHAKE_NONCE_SIZE bytes" }
        return ByteBuffer.allocate(HELLO_PACKET_SIZE)
            .put(type)
            .put(publicKey)
            .put(nonce)
            .array()
    }

    fun parseHello(data: ByteArray): HelloPacket? {
        if (data.size < HELLO_PACKET_SIZE) return null
        val publicKey = data.copyOfRange(1, 1 + PUBLIC_KEY_SIZE)
        val nonce = data.copyOfRange(1 + PUBLIC_KEY_SIZE, HELLO_PACKET_SIZE)
        return HelloPacket(publicKey, nonce)
    }

    fun buildSecureEnvelope(innerType: Byte, counter: Long, tag: ByteArray, body: ByteArray): ByteArray {
        require(tag.size == SECURE_ENVELOPE_TAG_SIZE) { "tag must be $SECURE_ENVELOPE_TAG_SIZE bytes" }
        return ByteBuffer.allocate(SECURE_ENVELOPE_HEADER_SIZE + body.size)
            .order(ByteOrder.BIG_ENDIAN)
            .put(innerType)
            .putLong(counter)
            .put(tag)
            .put(body)
            .array()
    }

    fun parseSecureEnvelope(data: ByteArray): SecureEnvelope? {
        if (data.size < SECURE_ENVELOPE_HEADER_SIZE) return null
        val buffer = ByteBuffer.wrap(data).order(ByteOrder.BIG_ENDIAN)
        val innerType = buffer.get()
        val counter = buffer.long
        val tag = ByteArray(SECURE_ENVELOPE_TAG_SIZE)
        buffer.get(tag)
        val body = data.copyOfRange(SECURE_ENVELOPE_HEADER_SIZE, data.size)
        return SecureEnvelope(innerType, counter, tag, body)
    }
}
