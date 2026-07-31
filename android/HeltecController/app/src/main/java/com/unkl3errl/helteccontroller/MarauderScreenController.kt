package com.unkl3errl.helteccontroller

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.text.method.ScrollingMovementMethod
import android.view.MotionEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import com.unkl3errl.helteccontroller.marauder.CommandRisk
import com.unkl3errl.helteccontroller.marauder.CommandSafety
import com.unkl3errl.helteccontroller.marauder.MarauderUsbSerial

@SuppressLint("ClickableViewAccessibility")
class MarauderScreenController(
    private val activity: Activity,
    private val root: View,
    private val setGlobalStatus: (String) -> Unit,
) : MarauderUsbSerial.Listener {
    private val connectionStatus: TextView = root.findViewById(R.id.marauderConnectionStatus)
    private val console: TextView = root.findViewById(R.id.marauderConsole)
    private val consoleLive: Button = root.findViewById(R.id.consoleLive)
    private val wifiSurveyButton: Button = root.findViewById(R.id.cmdWifiScan)
    private val commandInput: EditText = root.findViewById(R.id.marauderCommand)
    private val serial = MarauderUsbSerial(activity, this)
    private val timedCommandHandler = Handler(Looper.getMainLooper())
    private val consoleBuffer = StringBuilder("Connect to begin.\n")
    private var consoleFollowing = true

    companion object {
        private const val WIFI_SURVEY_DURATION_MS = 18_000L
        private const val COMMAND_SETTLE_MS = 400L
    }

    init {
        console.movementMethod = ScrollingMovementMethod()
        console.setOnTouchListener { view, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    setConsoleFollowing(false)
                    view.parent.requestDisallowInterceptTouchEvent(true)
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL ->
                    view.parent.requestDisallowInterceptTouchEvent(false)
            }
            false
        }
        root.findViewById<Button>(R.id.marauderConnect).setOnClickListener {
            setGlobalStatus("USB CONNECTING…")
            serial.connect()
        }
        root.findViewById<Button>(R.id.marauderDisconnect).setOnClickListener {
            cancelTimedWifiSurvey()
            serial.close()
            onSerialStatus("Marauder USB disconnected", false)
        }
        root.findViewById<Button>(R.id.consoleClear).setOnClickListener {
            consoleBuffer.clear()
            console.text = ""
            setConsoleFollowing(true)
            console.scrollTo(0, 0)
        }
        root.findViewById<Button>(R.id.consolePageUp).setOnClickListener {
            scrollConsoleBy(-(console.height * 3 / 4).coerceAtLeast(1))
        }
        root.findViewById<Button>(R.id.consolePageDown).setOnClickListener {
            scrollConsoleBy((console.height * 3 / 4).coerceAtLeast(1))
        }
        consoleLive.setOnClickListener {
            setConsoleFollowing(true)
            console.post(::scrollConsoleToBottom)
        }
        root.findViewById<Button>(R.id.marauderSend).setOnClickListener { sendInput() }
        commandInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_SEND) {
                sendInput()
                true
            } else false
        }

        bindCommand(R.id.cmdHelp, "help")
        bindCommand(R.id.cmdGpsFix, "gps -g fix")
        bindCommand(R.id.cmdGpsData, "gpsdata")
        wifiSurveyButton.setOnClickListener { startTimedWifiSurvey() }
        bindCommand(R.id.cmdBleScan, "sniffbt")
        root.findViewById<Button>(R.id.cmdStop).setOnClickListener {
            cancelTimedWifiSurvey()
            sendGuarded("stopscan")
        }
        root.findViewById<Button>(R.id.cmdListAp).setOnClickListener {
            stopAndListAccessPoints()
        }
        bindCommand(R.id.cmdListBle, "list -b")
        bindCommand(R.id.cmdPacketCount, "packetcount")
    }

    fun destroy() {
        cancelTimedWifiSurvey()
        serial.destroy()
    }

    override fun onSerialStatus(message: String, connected: Boolean) {
        activity.runOnUiThread {
            if (!connected) cancelTimedWifiSurvey()
            connectionStatus.text = message
            setGlobalStatus(if (connected) "MARAUDER USB" else "IDLE")
            appendConsole("\n[link] $message\n")
        }
    }

    override fun onSerialData(data: ByteArray) {
        val text = data.toString(Charsets.UTF_8)
        activity.runOnUiThread { appendConsole(text) }
    }

    override fun onSerialError(message: String) {
        activity.runOnUiThread {
            appendConsole("\n[error] $message\n")
            Toast.makeText(activity, message, Toast.LENGTH_LONG).show()
            setGlobalStatus("USB ERROR")
        }
    }

    private fun bindCommand(buttonId: Int, command: String) {
        root.findViewById<Button>(buttonId).setOnClickListener { sendGuarded(command) }
    }

    private fun startTimedWifiSurvey() {
        if (!serial.isConnected) {
            Toast.makeText(activity, "Connect the Marauder USB device first", Toast.LENGTH_LONG).show()
            return
        }
        cancelTimedWifiSurvey()
        wifiSurveyButton.text = "SCANNING…"
        appendConsole("\n[survey] Clearing the old AP list…\n")
        send("clearlist -a")
        timedCommandHandler.postDelayed({
            if (!serial.isConnected) return@postDelayed
            appendConsole("\n[survey] Scanning every 2.4 GHz channel for 18 seconds…\n")
            send("scanall")
            timedCommandHandler.postDelayed(::finishTimedWifiSurvey, WIFI_SURVEY_DURATION_MS)
        }, COMMAND_SETTLE_MS)
    }

    private fun finishTimedWifiSurvey() {
        wifiSurveyButton.text = "AP SURVEY 18S"
        if (!serial.isConnected) return
        appendConsole("\n[survey] Scan complete; stopping and listing access points…\n")
        send("stopscan")
        timedCommandHandler.postDelayed({
            if (serial.isConnected) send("list -a")
        }, COMMAND_SETTLE_MS)
    }

    private fun stopAndListAccessPoints() {
        if (!serial.isConnected) {
            Toast.makeText(activity, "Connect the Marauder USB device first", Toast.LENGTH_LONG).show()
            return
        }
        cancelTimedWifiSurvey()
        appendConsole("\n[survey] Stopping the current scan before listing access points…\n")
        send("stopscan")
        timedCommandHandler.postDelayed({
            if (serial.isConnected) send("list -a")
        }, COMMAND_SETTLE_MS)
    }

    private fun cancelTimedWifiSurvey() {
        timedCommandHandler.removeCallbacksAndMessages(null)
        wifiSurveyButton.text = "AP SURVEY 18S"
    }

    private fun sendInput() {
        val command = commandInput.text.toString().trim()
        if (command.isBlank()) return
        sendGuarded(command) { commandInput.text.clear() }
    }

    private fun sendGuarded(command: String, sent: () -> Unit = {}) {
        if (!serial.isConnected) {
            Toast.makeText(activity, "Connect the Marauder USB device first", Toast.LENGTH_LONG).show()
            return
        }
        when (CommandSafety.classify(command)) {
            CommandRisk.SAFE -> send(command, sent)
            CommandRisk.CONFIRM -> AlertDialog.Builder(activity)
                .setTitle("Send unclassified command?")
                .setMessage("The app cannot determine the radio impact of:\n\n$command\n\nReview it before continuing.")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Send") { _, _ -> send(command, sent) }
                .show()
            CommandRisk.ACTIVE -> typedCommandConfirmation(command, sent)
        }
    }

    private fun typedCommandConfirmation(command: String, sent: () -> Unit) {
        val input = EditText(activity).apply {
            hint = "AUTHORIZE"
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
            setPadding(48, 16, 48, 16)
        }
        val dialog = AlertDialog.Builder(activity)
            .setTitle("Authorize active command")
            .setMessage(
                "This command may transmit, alter network state, contact another system, or restart the device:\n\n$command\n\nUse only with authorization. Type AUTHORIZE to send it.",
            )
            .setView(input)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Send", null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                if (input.text.toString().trim() == "AUTHORIZE") {
                    dialog.dismiss()
                    send(command, sent)
                } else {
                    input.error = "Type AUTHORIZE exactly"
                }
            }
        }
        dialog.show()
    }

    private fun send(command: String, sent: () -> Unit = {}) {
        appendConsole("\n> $command\n")
        serial.writeCommand(command)
        sent()
    }

    private fun appendConsole(text: String) {
        consoleBuffer.append(text.replace("\u0000", ""))
        if (consoleBuffer.length > 30_000) {
            consoleBuffer.delete(0, consoleBuffer.length - 24_000)
        }
        console.text = consoleBuffer.toString()
        if (consoleFollowing) console.post(::scrollConsoleToBottom)
    }

    private fun scrollConsoleBy(delta: Int) {
        setConsoleFollowing(false)
        console.scrollTo(0, (console.scrollY + delta).coerceIn(0, maxConsoleScroll()))
    }

    private fun scrollConsoleToBottom() {
        console.scrollTo(0, maxConsoleScroll())
    }

    private fun maxConsoleScroll(): Int {
        val layout = console.layout ?: return 0
        return (
            layout.getLineTop(console.lineCount) - console.height +
                console.totalPaddingTop + console.totalPaddingBottom
            ).coerceAtLeast(0)
    }

    private fun setConsoleFollowing(following: Boolean) {
        consoleFollowing = following
        consoleLive.text = if (following) "LIVE ON" else "LIVE"
    }
}
