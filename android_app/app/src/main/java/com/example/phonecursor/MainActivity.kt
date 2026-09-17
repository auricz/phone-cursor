package com.example.phonecursor

import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.View
import android.view.inputmethod.InputMethodManager
import androidx.appcompat.app.AppCompatActivity
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
