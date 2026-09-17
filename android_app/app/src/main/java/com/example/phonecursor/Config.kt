package com.example.phonecursor

/**
 * Central place for every tunable value used by the Android app.
 * Keep networking, protocol and motion-tuning constants here instead of
 * scattering literals through the code, per project convention.
 */
object Config {

    // Networking
    const val DEFAULT_PORT = 24800
    const val SOCKET_TIMEOUT_MS = 2000

    // How often we sample/send sensor data to the desktop server.
    const val SEND_RATE_HZ = 60
    const val SEND_INTERVAL_MS = 1000L / SEND_RATE_HZ

    // Motion tracking (see MotionTracker). The phone only relays its
    // current orientation - all angle-to-cursor mapping happens on the
    // desktop (see desktop_server's Config.h / MotionProcessor), per project
    // convention that motion tuning lives centrally with the code that uses it.

    // Max angle sliders (see MainActivity): how far the user must turn
    // (yaw) or tilt (pitch) the phone to reach the screen edge. Sent to the
    // desktop as a fraction of a 90-degree turn (see Protocol.config and
    // desktop_server's MotionProcessor).
    const val MIN_MAX_ANGLE_DEGREES = 5
    const val MAX_MAX_ANGLE_DEGREES = 90
    const val DEFAULT_MAX_YAW_ANGLE_DEGREES = 90
    const val DEFAULT_MAX_PITCH_ANGLE_DEGREES = 90
    const val QUARTER_TURN_DEGREES = 90f

    // Whether the phone flips its sensor data before sending, so turning the
    // opposite direction moves the cursor the way the user expects.
    const val DEFAULT_INVERT_YAW = false
    const val DEFAULT_INVERT_PITCH = false

    // Minimum orientation change (degrees) required before a new sensor
    // sample is sent. Below this the phone is considered "not moving", so no
    // packet is sent - this lets the user move the desktop mouse by hand
    // without the phone's last reading fighting it.
    const val MOTION_EPSILON_DEGREES = 0.8
}
