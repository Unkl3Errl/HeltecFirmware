package com.unkl3errl.helteccontroller

import android.app.Activity
import android.app.AlertDialog
import android.text.InputType
import android.text.method.ScrollingMovementMethod
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import com.unkl3errl.helteccontroller.marauder.CommandRisk
import com.unkl3errl.helteccontroller.marauder.CommandSafety
import com.unkl3errl.helteccontroller.marauder.MarauderUsbSerial

class MarauderScreenController(
    private val activity: Activity,
    private val root: View,
    private val setGlobalStatus: (String) -> Unit,
) : MarauderUsbSerial.Listener {
    private val connectionStatus: TextView = root.findViewById(R.id.marauderConnectionStatus)
    private val console: TextView = root.findViewById(R.id.marauderConsole)
    private val commandInput: EditText = root.findViewById(R.id.marauderCommand)
    private val serial = MarauderUsbSerial(activity, this)
    private val consoleBuffer = StringBuilder("Connect to begin.\n")

    init {
        console.movementMethod = ScrollingMovementMethod()
        root.findViewById<Button>(R.id.marauderConnect).setOnClickListener {
            setGlobalStatus("USB CONNECTING…")
            serial.connect()
        }
        root.findViewById<Button>(R.id.marauderDisconnect).setOnClickListener {
            serial.close()
            onSerialStatus("Marauder USB disconnected", false)
        }
        root.findViewById<Button>(R.id.consoleClear).setOnClickListener {
            consoleBuffer.clear()
            console.text = ""
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
        bindCommand(R.id.cmdWifiScan, "scanall")
        bindCommand(R.id.cmdBleScan, "sniffbt")
        bindCommand(R.id.cmdStop, "stopscan")
        bindCommand(R.id.cmdListAp, "list -a")
        bindCommand(R.id.cmdListBle, "list -b")
        bindCommand(R.id.cmdPacketCount, "packetcount")
    }

    fun destroy() = serial.destroy()

    override fun onSerialStatus(message: String, connected: Boolean) {
        activity.runOnUiThread {
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

    private fun send(command: String, sent: () -> Unit) {
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
        console.post {
            val layout = console.layout ?: return@post
            val scrollAmount = layout.getLineTop(console.lineCount) - console.height
            console.scrollTo(0, scrollAmount.coerceAtLeast(0))
        }
    }
}
