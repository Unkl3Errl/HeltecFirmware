package com.unkl3errl.helteccontroller

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.text.method.ScrollingMovementMethod
import android.view.MotionEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.FileProvider
import com.unkl3errl.helteccontroller.marauder.CommandRisk
import com.unkl3errl.helteccontroller.marauder.CommandSafety
import com.unkl3errl.helteccontroller.marauder.MarauderAccessPoint
import com.unkl3errl.helteccontroller.marauder.MarauderBleDevice
import com.unkl3errl.helteccontroller.marauder.MarauderResultParser
import com.unkl3errl.helteccontroller.marauder.MarauderSession
import com.unkl3errl.helteccontroller.marauder.MarauderSessionStore
import com.unkl3errl.helteccontroller.marauder.MarauderUsbSerial
import java.io.File
import java.text.DateFormat
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.util.Date

@SuppressLint("ClickableViewAccessibility")
class MarauderScreenController(
    private val activity: Activity,
    private val root: View,
    private val requestExport: (MarauderExportRequest) -> Unit,
    private val setGlobalStatus: (String) -> Unit,
) : MarauderUsbSerial.Listener {
    private val connectionStatus: TextView = root.findViewById(R.id.marauderConnectionStatus)
    private val recordingStatus: TextView = root.findViewById(R.id.marauderRecordingStatus)
    private val apResultsStatus: TextView = root.findViewById(R.id.marauderApResultsStatus)
    private val bleResultsStatus: TextView = root.findViewById(R.id.marauderBleResultsStatus)
    private val console: TextView = root.findViewById(R.id.marauderConsole)
    private val consoleLive: Button = root.findViewById(R.id.consoleLive)
    private val wifiSurveyButton: Button = root.findViewById(R.id.cmdWifiScan)
    private val commandInput: EditText = root.findViewById(R.id.marauderCommand)
    private val serial = MarauderUsbSerial(activity, this)
    private val sessionStore = MarauderSessionStore(File(activity.filesDir, "marauder_sessions"))
    private val resultParser = MarauderResultParser()
    private val timedCommandHandler = Handler(Looper.getMainLooper())
    private val consoleBuffer = StringBuilder("Connect to begin.\n")
    private var consoleFollowing = true

    companion object {
        private const val WIFI_SURVEY_DURATION_MS = 18_000L
        private const val COMMAND_SETTLE_MS = 400L
        private const val MAX_SESSION_PREVIEW_CHARS = 80_000
        private val EXPORT_STAMP = DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss")
            .withZone(ZoneOffset.UTC)
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
        root.findViewById<Button>(R.id.cmdBleScan).setOnClickListener {
            resultParser.clearBleDevices()
            updateStructuredResults()
            sendGuarded("sniffbt")
        }
        root.findViewById<Button>(R.id.cmdStop).setOnClickListener {
            cancelTimedWifiSurvey()
            sendGuarded("stopscan")
        }
        root.findViewById<Button>(R.id.cmdListAp).setOnClickListener {
            stopAndListAccessPoints()
        }
        root.findViewById<Button>(R.id.cmdListBle).setOnClickListener { sendGuarded("list -b") }
        bindCommand(R.id.cmdPacketCount, "packetcount")
        root.findViewById<Button>(R.id.marauderViewApResults).setOnClickListener {
            showAccessPointResults()
        }
        root.findViewById<Button>(R.id.marauderExportApCsv).setOnClickListener {
            exportAccessPoints()
        }
        root.findViewById<Button>(R.id.marauderViewBleResults).setOnClickListener {
            showBleResults()
        }
        root.findViewById<Button>(R.id.marauderExportBleCsv).setOnClickListener {
            exportBleDevices()
        }
        root.findViewById<Button>(R.id.marauderSessionHistory).setOnClickListener {
            showSessionHistory()
        }
        root.findViewById<Button>(R.id.marauderExportConsole).setOnClickListener {
            requestTextExport(
                "marauder-console-${EXPORT_STAMP.format(Instant.now())}.txt",
                consoleBuffer.toString(),
            )
        }
        updateRecordingStatus()
        updateStructuredResults()
    }

    fun destroy() {
        cancelTimedWifiSurvey()
        sessionStore.append("SESSION", "Controller activity closed")
        sessionStore.stop()
        serial.destroy()
    }

    fun onExportSaved(fileName: String) {
        setGlobalStatus(if (serial.isConnected) "MARAUDER USB" else "IDLE")
        Toast.makeText(activity, "Saved $fileName", Toast.LENGTH_LONG).show()
    }

    fun onExportCancelled() {
        setGlobalStatus(if (serial.isConnected) "MARAUDER USB" else "IDLE")
    }

    fun onExportError(message: String) {
        setGlobalStatus("EXPORT ERROR")
        Toast.makeText(activity, message, Toast.LENGTH_LONG).show()
    }

    override fun onSerialStatus(message: String, connected: Boolean) {
        val sessionError = runCatching {
            if (connected) {
                sessionStore.start()
                sessionStore.append("LINK", message)
            } else if (sessionStore.current() != null) {
                sessionStore.append("LINK", message)
                sessionStore.stop()
            }
        }.exceptionOrNull()
        activity.runOnUiThread {
            if (!connected) cancelTimedWifiSurvey()
            connectionStatus.text = message
            updateRecordingStatus(sessionError?.message)
            setGlobalStatus(if (connected) "MARAUDER USB" else "IDLE")
            appendConsole("\n[link] $message\n")
        }
    }

    override fun onSerialData(data: ByteArray) {
        val text = data.toString(Charsets.UTF_8)
        sessionStore.append("RX", text)
        activity.runOnUiThread {
            if (resultParser.consume(text)) updateStructuredResults()
            updateRecordingStatus()
            appendConsole(text)
        }
    }

    override fun onSerialError(message: String) {
        sessionStore.append("ERROR", message)
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
        resultParser.clearAccessPoints()
        updateStructuredResults()
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
        sessionStore.append("TX", command)
        updateRecordingStatus()
        serial.writeCommand(command)
        sent()
    }

    private fun updateRecordingStatus(error: String? = null) {
        val active = sessionStore.current()
        when {
            error != null -> {
                recordingStatus.text = "Session recording error · $error"
                recordingStatus.setTextColor(activity.getColor(R.color.orange))
            }
            active != null -> {
                recordingStatus.text = "● RECORDING · ${active.fileName} · ${formatBytes(active.sizeBytes)}"
                recordingStatus.setTextColor(activity.getColor(R.color.teal))
            }
            else -> {
                recordingStatus.text = "Session recording starts automatically with the USB link"
                recordingStatus.setTextColor(activity.getColor(R.color.muted))
            }
        }
    }

    private fun updateStructuredResults() {
        val accessPoints = resultParser.accessPoints()
        val bleDevices = resultParser.bleDevices()
        apResultsStatus.text = if (accessPoints.isEmpty()) {
            "Access points · no structured results yet"
        } else {
            val strongest = accessPoints.maxByOrNull(MarauderAccessPoint::rssi)
            "Access points · ${accessPoints.size} · strongest ${strongest?.ssid} ${strongest?.rssi} dBm"
        }
        bleResultsStatus.text = if (bleDevices.isEmpty()) {
            "BLE devices · no structured results yet"
        } else {
            val strongest = bleDevices.maxByOrNull(MarauderBleDevice::rssi)
            "BLE devices · ${bleDevices.size} · strongest ${strongest?.name} ${strongest?.rssi} dBm"
        }
    }

    private fun showAccessPointResults() {
        val accessPoints = resultParser.accessPoints()
        if (accessPoints.isEmpty()) {
            AlertDialog.Builder(activity)
                .setTitle("Access points")
                .setMessage("No structured AP results are available. Run AP Survey 18s first.")
                .setPositiveButton("OK", null)
                .show()
            return
        }
        val lines = accessPoints.map { ap ->
            "CH ${ap.channel.toString().padStart(2)}  ${ap.rssi.toString().padStart(4)} dBm  ${ap.ssid}" +
                if (ap.selected) "  [selected]" else ""
        }
        AlertDialog.Builder(activity)
            .setTitle("Access points · ${accessPoints.size}")
            .setItems(lines.toTypedArray(), null)
            .setPositiveButton("Close", null)
            .show()
    }

    private fun showBleResults() {
        val bleDevices = resultParser.bleDevices()
        if (bleDevices.isEmpty()) {
            AlertDialog.Builder(activity)
                .setTitle("BLE devices")
                .setMessage("No structured BLE results are available. Run BLE Scan, stop it, then tap List BLE.")
                .setPositiveButton("OK", null)
                .show()
            return
        }
        val lines = bleDevices.map { device ->
            "${device.rssi.toString().padStart(4)} dBm  ${device.name}"
        }
        AlertDialog.Builder(activity)
            .setTitle("BLE devices · ${bleDevices.size}")
            .setItems(lines.toTypedArray(), null)
            .setPositiveButton("Close", null)
            .show()
    }

    private fun exportAccessPoints() {
        if (resultParser.accessPoints().isEmpty()) {
            Toast.makeText(activity, "Run an AP survey before exporting", Toast.LENGTH_LONG).show()
            return
        }
        requestTextExport(
            "marauder-access-points-${EXPORT_STAMP.format(Instant.now())}.csv",
            resultParser.accessPointsCsv(),
            "text/csv",
        )
    }

    private fun exportBleDevices() {
        if (resultParser.bleDevices().isEmpty()) {
            Toast.makeText(activity, "Collect and list BLE devices before exporting", Toast.LENGTH_LONG).show()
            return
        }
        requestTextExport(
            "marauder-ble-devices-${EXPORT_STAMP.format(Instant.now())}.csv",
            resultParser.bleDevicesCsv(),
            "text/csv",
        )
    }

    private fun showSessionHistory() {
        val sessions = sessionStore.sessions()
        if (sessions.isEmpty()) {
            AlertDialog.Builder(activity)
                .setTitle("Marauder sessions")
                .setMessage("No saved USB sessions yet. Recording begins automatically after Connect succeeds.")
                .setPositiveButton("OK", null)
                .show()
            return
        }
        val activeName = sessionStore.current()?.fileName
        val labels = sessions.map { session ->
            buildString {
                append(session.fileName)
                if (session.fileName == activeName) append("  ● recording")
                append("\n")
                append(DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT)
                    .format(Date(session.modifiedAt)))
                append(" · ").append(formatBytes(session.sizeBytes))
            }
        }
        AlertDialog.Builder(activity)
            .setTitle("Marauder sessions · ${sessions.size}")
            .setItems(labels.toTypedArray()) { _, index -> showSessionActions(sessions[index]) }
            .setNegativeButton("Close", null)
            .show()
    }

    private fun showSessionActions(session: MarauderSession) {
        val isActive = sessionStore.current()?.fileName == session.fileName
        val actions = if (isActive) {
            arrayOf("View", "Export TXT", "Share")
        } else {
            arrayOf("View", "Rename", "Export TXT", "Share", "Delete")
        }
        AlertDialog.Builder(activity)
            .setTitle(session.fileName)
            .setItems(actions) { _, index ->
                when (actions[index]) {
                    "View" -> previewSession(session.fileName)
                    "Rename" -> renameSession(session.fileName)
                    "Export TXT" -> exportSession(session.fileName)
                    "Share" -> shareSession(session.fileName)
                    "Delete" -> confirmDeleteSession(session.fileName)
                }
            }
            .setNegativeButton("Close", null)
            .show()
    }

    private fun previewSession(fileName: String) {
        runCatching { sessionStore.read(fileName) }
            .onSuccess { fullText ->
                val preview = if (fullText.length > MAX_SESSION_PREVIEW_CHARS) {
                    "[Showing the newest $MAX_SESSION_PREVIEW_CHARS characters]\n\n" +
                        fullText.takeLast(MAX_SESSION_PREVIEW_CHARS)
                } else fullText
                val textView = TextView(activity).apply {
                    text = preview
                    setTextColor(activity.getColor(R.color.text))
                    textSize = 12f
                    typeface = android.graphics.Typeface.MONOSPACE
                    setTextIsSelectable(true)
                    setPadding(32, 16, 32, 16)
                }
                val scroll = ScrollView(activity).apply { addView(textView) }
                AlertDialog.Builder(activity)
                    .setTitle(fileName)
                    .setView(scroll)
                    .setPositiveButton("Close", null)
                    .show()
            }
            .onFailure { toastError(it) }
    }

    private fun renameSession(fileName: String) {
        val input = EditText(activity).apply {
            setText(fileName.removeSuffix(".txt"))
            selectAll()
            inputType = InputType.TYPE_CLASS_TEXT
        }
        AlertDialog.Builder(activity)
            .setTitle("Rename session")
            .setView(input)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Rename") { _, _ ->
                runCatching { sessionStore.rename(fileName, input.text.toString()) }
                    .onSuccess { Toast.makeText(activity, "Renamed to ${it.fileName}", Toast.LENGTH_LONG).show() }
                    .onFailure { toastError(it) }
            }
            .show()
    }

    private fun exportSession(fileName: String) {
        runCatching { sessionStore.file(fileName).readBytes() }
            .onSuccess { bytes ->
                setGlobalStatus("EXPORTING…")
                requestExport(MarauderExportRequest(fileName, "text/plain", bytes))
            }
            .onFailure { toastError(it) }
    }

    private fun shareSession(fileName: String) {
        runCatching {
            val file = sessionStore.file(fileName)
            val uri = FileProvider.getUriForFile(activity, "${activity.packageName}.files", file)
            val intent = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_STREAM, uri)
                clipData = ClipData.newUri(activity.contentResolver, fileName, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            activity.startActivity(Intent.createChooser(intent, "Share Marauder session"))
        }.onFailure { toastError(it) }
    }

    private fun confirmDeleteSession(fileName: String) {
        AlertDialog.Builder(activity)
            .setTitle("Delete session?")
            .setMessage(fileName)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Delete") { _, _ ->
                runCatching { sessionStore.delete(fileName) }
                    .onSuccess { Toast.makeText(activity, "Deleted $fileName", Toast.LENGTH_LONG).show() }
                    .onFailure { toastError(it) }
            }
            .show()
    }

    private fun requestTextExport(fileName: String, content: String, mimeType: String = "text/plain") {
        setGlobalStatus("EXPORTING…")
        requestExport(MarauderExportRequest(fileName, mimeType, content.toByteArray(Charsets.UTF_8)))
    }

    private fun toastError(error: Throwable) {
        Toast.makeText(activity, error.message ?: error.javaClass.simpleName, Toast.LENGTH_LONG).show()
    }

    private fun formatBytes(bytes: Long): String = when {
        bytes >= 1_048_576 -> String.format("%.1f MB", bytes / 1_048_576.0)
        bytes >= 1_024 -> String.format("%.1f KB", bytes / 1_024.0)
        else -> "$bytes B"
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
