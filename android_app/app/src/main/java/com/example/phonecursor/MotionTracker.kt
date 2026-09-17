package com.example.phonecursor

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.Looper

/**
 * Relays the phone's absolute orientation to the desktop as raw sensor
 * data; the desktop turns it into an absolute cursor position (see
 * desktop_server's MotionProcessor). This class is only responsible for:
 *  - reading device orientation (game rotation vector, chosen over the
 *    compass-backed rotation vector so a magnetic disturbance can't
 *    suddenly wrench the tracked direction),
 *  - sending it at a fixed rate while running, and
 *  - producing a CALIBRATE reading on demand (used once on connect and
 *    again whenever the user taps "Recalibrate"), which the desktop uses to
 *    learn which pose counts as "centered": phone lying flat, screen up,
 *    top edge pointed at the screen.
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

    private val handler = Handler(Looper.getMainLooper())
    private var running = false

    private val tickRunnable = object : Runnable {
        override fun run() {
            onSensorSample(
                latestQuaternion[0], latestQuaternion[1], latestQuaternion[2], latestQuaternion[3]
            )
            if (running) {
                handler.postDelayed(this, Config.SEND_INTERVAL_MS)
            }
        }
    }

    fun start() {
        if (running) return
        running = true

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
        onCalibrate(latestQuaternion[0], latestQuaternion[1], latestQuaternion[2], latestQuaternion[3])
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
