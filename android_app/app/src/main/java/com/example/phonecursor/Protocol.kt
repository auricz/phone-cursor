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
 */
object Protocol {

    private const val TYPE_SENSOR: Byte = 1
    private const val TYPE_CLICK: Byte = 2
    private const val TYPE_KEY: Byte = 3
    private const val TYPE_CALIBRATE: Byte = 4
    private const val TYPE_CONFIG: Byte = 5

    const val BUTTON_LEFT: Byte = 0
    const val BUTTON_RIGHT: Byte = 1

    const val KEY_ACTION_CHAR: Byte = 0
    const val KEY_ACTION_BACKSPACE: Byte = 1

    const val CONFIG_OPT_YAW_MAX_PERCENT: Byte = 0
    const val CONFIG_OPT_PITCH_MAX_PERCENT: Byte = 1

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
}
