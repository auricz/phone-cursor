package com.example.phonecursor

import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.TextView
import androidx.annotation.StringRes
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.lifecycleScope
import com.example.phonecursor.databinding.ActivityMainBinding
import kotlinx.coroutines.launch
import java.io.IOException

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var networkClient: NetworkClient
    private lateinit var motionTracker: MotionTracker

    private var previousKeyboardText = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        networkClient = NetworkClient()
        motionTracker = MotionTracker(this, ::onSensorSample, ::onCalibrate)

        binding.portInput.setText(Config.DEFAULT_PORT.toString())
        binding.connectButton.setOnClickListener { onConnectClicked() }
        binding.disconnectButton.setOnClickListener { disconnect() }
        binding.leftClickButton.setOnClickListener {
            networkClient.send(Protocol.click(Protocol.BUTTON_LEFT))
        }
        binding.rightClickButton.setOnClickListener {
            networkClient.send(Protocol.click(Protocol.BUTTON_RIGHT))
        }
        binding.keyboardButton.setOnClickListener { showKeyboard() }
        binding.recalibrateButton.setOnClickListener { motionTracker.calibrate() }
        binding.hiddenKeyboardInput.addTextChangedListener(keyboardWatcher)
        setUpMotionSettings()
        hideNavigationBar()
    }

    /** Hides the system navigation bar so it can't cover on-screen buttons; swipe reveals it again. */
    private fun hideNavigationBar() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val insetsController = WindowInsetsControllerCompat(window, binding.root)
        insetsController.hide(WindowInsetsCompat.Type.navigationBars())
        insetsController.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // A transient swipe-reveal (see hideNavigationBar) only hides the bar again on its own
        // after user interaction elsewhere; re-hide explicitly once we regain focus.
        if (hasFocus) {
            hideNavigationBar()
        }
    }

    /** Wires up the max-angle sliders and invert checkboxes to MotionTracker and the desktop. */
    private fun setUpMotionSettings() {
        binding.maxYawSlider.valueFrom = Config.MIN_MAX_ANGLE_DEGREES.toFloat()
        binding.maxYawSlider.valueTo = Config.MAX_MAX_ANGLE_DEGREES.toFloat()
        binding.maxYawSlider.stepSize = 1f
        binding.maxYawSlider.value = Config.DEFAULT_MAX_YAW_ANGLE_DEGREES.toFloat()

        binding.maxPitchSlider.valueFrom = Config.MIN_MAX_ANGLE_DEGREES.toFloat()
        binding.maxPitchSlider.valueTo = Config.MAX_MAX_ANGLE_DEGREES.toFloat()
        binding.maxPitchSlider.stepSize = 1f
        binding.maxPitchSlider.value = Config.DEFAULT_MAX_PITCH_ANGLE_DEGREES.toFloat()

        updateMaxAngleLabel(binding.maxYawLabel, R.string.label_max_yaw_angle, binding.maxYawSlider.value)
        updateMaxAngleLabel(binding.maxPitchLabel, R.string.label_max_pitch_angle, binding.maxPitchSlider.value)

        binding.maxYawSlider.addOnChangeListener { _, value, _ ->
            updateMaxAngleLabel(binding.maxYawLabel, R.string.label_max_yaw_angle, value)
            sendMaxAngleConfig(Protocol.CONFIG_OPT_YAW_MAX_PERCENT, value)
        }
        binding.maxPitchSlider.addOnChangeListener { _, value, _ ->
            updateMaxAngleLabel(binding.maxPitchLabel, R.string.label_max_pitch_angle, value)
            sendMaxAngleConfig(Protocol.CONFIG_OPT_PITCH_MAX_PERCENT, value)
        }

        binding.invertYawCheckbox.isChecked = Config.DEFAULT_INVERT_YAW
        binding.invertPitchCheckbox.isChecked = Config.DEFAULT_INVERT_PITCH
        motionTracker.invertYaw = binding.invertYawCheckbox.isChecked
        motionTracker.invertPitch = binding.invertPitchCheckbox.isChecked

        binding.invertYawCheckbox.setOnCheckedChangeListener { _, isChecked ->
            motionTracker.invertYaw = isChecked
        }
        binding.invertPitchCheckbox.setOnCheckedChangeListener { _, isChecked ->
            motionTracker.invertPitch = isChecked
        }
    }

    private fun updateMaxAngleLabel(label: TextView, @StringRes resId: Int, degrees: Float) {
        label.text = getString(resId, degrees.toInt())
    }

    /** Sends the current slider value (degrees) to the desktop as a fraction of a 90-degree turn. */
    private fun sendMaxAngleConfig(option: Byte, degrees: Float) {
        networkClient.send(Protocol.config(option, degrees / Config.QUARTER_TURN_DEGREES))
    }

    override fun onPause() {
        super.onPause()
        motionTracker.stop()
    }

    override fun onResume() {
        super.onResume()
        if (networkClient.isConnected) {
            motionTracker.start()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        networkClient.disconnect()
    }

    private fun onConnectClicked() {
        val host = binding.ipInput.text.toString().trim()
        val portText = binding.portInput.text.toString().trim()
        val port = portText.toIntOrNull()

        if (host.isEmpty()) {
            binding.statusText.text = getString(R.string.status_error_invalid_ip)
            return
        }
        if (port == null || port !in 1..65535) {
            binding.statusText.text = getString(R.string.status_error_invalid_port)
            return
        }

        binding.statusText.text = getString(R.string.status_connecting)
        lifecycleScope.launch {
            val error = try {
                networkClient.connect(host, port)
                null
            } catch (e: IOException) {
                e.message ?: "Connection failed"
            }

            if (error != null) {
                binding.statusText.text = error
                return@launch
            }

            binding.statusText.text = getString(R.string.status_connected, host, port)
            binding.connectionPanel.visibility = View.GONE
            binding.controlsPanel.visibility = View.VISIBLE
            // Push the sliders' current values: the desktop has no memory of
            // a prior session, so this keeps it in sync with what's on screen.
            sendMaxAngleConfig(Protocol.CONFIG_OPT_YAW_MAX_PERCENT, binding.maxYawSlider.value)
            sendMaxAngleConfig(Protocol.CONFIG_OPT_PITCH_MAX_PERCENT, binding.maxPitchSlider.value)
            motionTracker.start()
            // Calibrate immediately: the connect flow asks the user to have
            // the phone lying flat, screen up, top edge toward the screen.
            motionTracker.calibrate()
        }
    }

    private fun disconnect() {
        motionTracker.stop()
        networkClient.disconnect()
        binding.statusText.text = getString(R.string.status_disconnected)
        binding.connectionPanel.visibility = View.VISIBLE
        binding.controlsPanel.visibility = View.GONE
    }

    private fun onSensorSample(qw: Float, qx: Float, qy: Float, qz: Float) {
        networkClient.send(Protocol.sensor(qw, qx, qy, qz))
    }

    private fun onCalibrate(qw: Float, qx: Float, qy: Float, qz: Float) {
        networkClient.send(Protocol.calibrate(qw, qx, qy, qz))
    }

    private fun showKeyboard() {
        previousKeyboardText = ""
        binding.hiddenKeyboardInput.setText("")
        binding.hiddenKeyboardInput.requestFocus()
        val imm = getSystemService(INPUT_METHOD_SERVICE) as InputMethodManager
        imm.showSoftInput(binding.hiddenKeyboardInput, InputMethodManager.SHOW_IMPLICIT)
    }

    /** Diffs each edit against the previous value and streams char/backspace key packets. */
    private val keyboardWatcher = object : TextWatcher {
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit

        override fun afterTextChanged(editable: Editable) {
            val newText = editable.toString()
            if (newText.length > previousKeyboardText.length && newText.startsWith(previousKeyboardText)) {
                for (i in previousKeyboardText.length until newText.length) {
                    networkClient.send(Protocol.keyChar(newText[i]))
                }
            } else if (newText.length < previousKeyboardText.length) {
                repeat(previousKeyboardText.length - newText.length) {
                    networkClient.send(Protocol.keyBackspace())
                }
            } else if (newText != previousKeyboardText) {
                // Non-trivial edit (e.g. autocorrect); resync by treating it as a full retype.
                repeat(previousKeyboardText.length) { networkClient.send(Protocol.keyBackspace()) }
                newText.forEach { networkClient.send(Protocol.keyChar(it)) }
            }
            previousKeyboardText = newText
        }
    }
}
