package com.example.phonecursor

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.Looper
import kotlin.math.abs
import kotlin.math.acos
import kotlin.math.min

/**
 * Relays the phone's absolute orientation to the desktop as raw sensor
 * data; the desktop turns it into an absolute cursor position (see
 * desktop_server's MotionProcessor). This class is only responsible for:
 *  - reading device orientation (game rotation vector, chosen over the
 *    compass-backed rotation vector so a magnetic disturbance can't
 *    suddenly wrench the tracked direction),
 *  - sending it at a fixed rate while running,
 *  - producing a CALIBRATE reading on demand (used once on connect and
 *    again whenever the user taps "Recalibrate"), which the desktop uses to
 *    learn which pose counts as "centered": phone lying flat, screen up,
 *    top edge pointed at the screen, and
 *  - flipping the outgoing data per the user's invert checkboxes (see
 *    [invertYaw]/[invertPitch]).
 */
class MotionTracker(
    context: Context,
    private val onSensorSample: (qw: Float, qx: Float, qy: Float, qz: Float) -> Unit,
    private val onCalibrate: (qw: Float, qx: Float, qy: Float, qz: Float) -> Unit
) : SensorEventListener {

    private val sensorManager =
        context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val gameRotationSensor: Sensor? =
        sensorManager.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)

    private val latestQuaternion = floatArrayOf(1f, 0f, 0f, 0f) // w, x, y, z; identity until first reading

    /** Set by MainActivity from the invert checkboxes; flips outgoing sensor data when true. */
    var invertYaw: Boolean = Config.DEFAULT_INVERT_YAW
    var invertPitch: Boolean = Config.DEFAULT_INVERT_PITCH

    private val handler = Handler(Looper.getMainLooper())
    private var running = false

    /** Orientation last actually sent, so a still phone can be skipped (see [hasMovedPastEpsilon]). */
    private var lastSentQuaternion: FloatArray? = null

    private val tickRunnable = object : Runnable {
        override fun run() {
            val current = invertedQuaternion()
            if (hasMovedPastEpsilon(current)) {
                lastSentQuaternion = current
                onSensorSample(current[0], current[1], current[2], current[3])
            }
            if (running) {
                handler.postDelayed(this, Config.SEND_INTERVAL_MS)
            }
        }
    }

    fun start() {
        if (running) return
        running = true
        lastSentQuaternion = null // force a fresh send so the desktop isn't left with a stale position

        gameRotationSensor?.let {
            sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_GAME)
        }
        handler.postDelayed(tickRunnable, Config.SEND_INTERVAL_MS)
    }

    fun stop() {
        running = false
        sensorManager.unregisterListener(this)
        handler.removeCallbacks(tickRunnable)
    }

    /** Reads the current orientation and reports it as the reference "phone pointed at the screen" pose. */
    fun calibrate() {
        val (w, x, y, z) = invertedQuaternion()
        onCalibrate(w, x, y, z)
    }

    /**
     * Applies the invert checkboxes to the latest reading before it's sent.
     * The desktop derives yaw/pitch from delta = reference^-1 * current (see
     * MotionProcessor), and negating a quaternion's (y, z) components is
     * equivalent to conjugating that rotation by a 180-degree turn about the
     * local right/pitch axis - which flips only the decomposed yaw angle, and
     * (being a conjugation) distributes cleanly over that reference/current
     * combination. Negating (x, y) is the same conjugation about the local
     * up/yaw axis instead, which flips only pitch. Applying it here, before
     * both CALIBRATE and SENSOR sends, keeps the reference and every sample
     * consistent.
     */
    private fun invertedQuaternion(): FloatArray {
        var w = latestQuaternion[0]
        var x = latestQuaternion[1]
        var y = latestQuaternion[2]
        var z = latestQuaternion[3]
        if (invertYaw) {
            y = -y
            z = -z
        }
        if (invertPitch) {
            x = -x
            y = -y
        }
        return floatArrayOf(w, x, y, z)
    }

    /**
     * True if [current] differs from [lastSentQuaternion] by at least
     * [Config.MOTION_EPSILON_DEGREES]. The angle between two unit
     * quaternions is 2*acos(|dot product|) - the absolute value accounts for
     * q and -q representing the same rotation.
     */
    private fun hasMovedPastEpsilon(current: FloatArray): Boolean {
        val last = lastSentQuaternion ?: return true
        val dot = last[0] * current[0] + last[1] * current[1] +
            last[2] * current[2] + last[3] * current[3]
        val angleDegrees = Math.toDegrees(2 * acos(min(1f, abs(dot)).toDouble()))
        return angleDegrees >= Config.MOTION_EPSILON_DEGREES
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type == Sensor.TYPE_GAME_ROTATION_VECTOR) {
            // Output is [w, x, y, z]; event.values may omit w (only 3 elements)
            // for this sensor type, so this derives it rather than assuming it's present.
            SensorManager.getQuaternionFromVector(latestQuaternion, event.values)
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) = Unit
}
