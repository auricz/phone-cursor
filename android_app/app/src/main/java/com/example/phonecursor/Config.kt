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
}
