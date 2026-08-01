package com.unkl3errl.helteccontroller

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.app.Dialog
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.net.ConnectivityManager
import android.location.Location
import android.net.Uri
import android.text.InputType
import android.view.View
import android.view.ViewGroup
import android.view.Window
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.unkl3errl.helteccontroller.bruce.BruceApiClient
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale
import java.util.concurrent.Executors

class BruceScreenController(
    private val activity: Activity,
    private val root: View,
    private val client: BruceApiClient,
    private val requestWifi: (String, String) -> Unit,
    private val requestPhoneGps: (Boolean) -> Unit,
    private val requestFieldLogExport: (String) -> Unit,
    private val setGlobalStatus: (String) -> Unit,
) {
    private data class UnifiedRefresh(
        val system: JSONObject,
        val capabilities: JSONObject,
        val logger: JSONObject,
        val lora: JSONObject,
        val wifi: JSONObject,
    )

    private val executor = Executors.newSingleThreadExecutor()
    private val connectivityManager =
        activity.getSystemService(ConnectivityManager::class.java)
    private val connectionStatus: TextView = root.findViewById(R.id.bruceConnectionStatus)
    private val systemStatus: TextView = root.findViewById(R.id.bruceSystemStatus)
    private val loggerStatus: TextView = root.findViewById(R.id.bruceLoggerStatus)
    private val loraStatus: TextView = root.findViewById(R.id.bruceLoraStatus)
    private val wifiSurveyStatus: TextView = root.findViewById(R.id.bruceWifiSurveyStatus)
    private val phoneGpsStatus: TextView = root.findViewById(R.id.brucePhoneGpsStatus)
    private val phoneGpsToggle: Button = root.findViewById(R.id.brucePhoneGpsToggle)
    private val ssid: EditText = root.findViewById(R.id.bruceSsid)
    private val wifiPassword: EditText = root.findViewById(R.id.bruceWifiPassword)
    private val baseUrl: EditText = root.findViewById(R.id.bruceBaseUrl)
    private val username: EditText = root.findViewById(R.id.bruceUsername)
    private val webPassword: EditText = root.findViewById(R.id.bruceWebPassword)
    private var loginAfterNetworkApproval = false
    private var phoneGpsEnabled = false
    private var webUiDialog: Dialog? = null

    init {
        root.findViewById<Button>(R.id.bruceJoinWifi).setOnClickListener {
            loginAfterNetworkApproval = true
            connectionStatus.text = "Waiting for Android to approve the device network…"
            setGlobalStatus("JOINING…")
            requestWifi(ssid.text.toString().trim(), wifiPassword.text.toString())
        }
        root.findViewById<Button>(R.id.bruceLogin).setOnClickListener { login() }
        root.findViewById<Button>(R.id.bruceRefresh).setOnClickListener { refreshAll() }
        root.findViewById<Button>(R.id.bruceGpsStart).setOnClickListener { gpsAction("start") }
        root.findViewById<Button>(R.id.bruceGpsStop).setOnClickListener { gpsAction("stop") }
        root.findViewById<Button>(R.id.bruceGpsTrack).setOnClickListener { showGpsTrack() }
        root.findViewById<Button>(R.id.bruceWifiScan).setOnClickListener { startWifiScan() }
        root.findViewById<Button>(R.id.bruceWifiResults).setOnClickListener { showWifiResults() }
        root.findViewById<Button>(R.id.bruceWifiClear).setOnClickListener { clearWifiResults() }
        root.findViewById<Button>(R.id.loggerStart).setOnClickListener { startLogger() }
        root.findViewById<Button>(R.id.loggerStop).setOnClickListener { loggerAction("stop") }
        root.findViewById<Button>(R.id.loggerFiles).setOnClickListener { showLoggerFiles() }
        phoneGpsToggle.setOnClickListener { requestPhoneGps(!phoneGpsEnabled) }
        root.findViewById<Button>(R.id.loraStart).setOnClickListener { startLora() }
        root.findViewById<Button>(R.id.loraStop).setOnClickListener { loraAction("stop") }
        root.findViewById<Button>(R.id.loraHistory).setOnClickListener { showLoraHistory() }
        root.findViewById<Button>(R.id.loraTransmit).setOnClickListener { confirmLoraTransmit() }
        root.findViewById<Button>(R.id.bruceOpenWebUi).setOnClickListener { openWebUi() }
        root.findViewById<Button>(R.id.bruceRestart).setOnClickListener {
            typedConfirmation(
                title = "Restart BruceNet?",
                message = "The device link will drop while the ESP32 restarts. Type RESTART to continue.",
                expected = "RESTART",
            ) { restartDevice() }
        }
    }

    fun onNetworkAvailable() {
        connectionStatus.text = "BruceNet local link available · logging in…"
        loginAfterNetworkApproval = false
        login()
    }

    fun onNetworkLost() {
        connectionStatus.text = "BruceNet local link was disconnected"
    }

    fun onNetworkError(message: String) {
        loginAfterNetworkApproval = false
        connectionStatus.text = message
        toast(message)
    }

    fun onPhoneGpsStarted() {
        phoneGpsEnabled = true
        phoneGpsToggle.text = "STOP PHONE GPS ASSIST"
        phoneGpsStatus.text = "Phone GPS assist active · waiting for a location fix"
    }

    fun onPhoneGpsStopped() {
        phoneGpsEnabled = false
        phoneGpsToggle.text = "START PHONE GPS ASSIST"
        phoneGpsStatus.text = "Phone GPS assist off · requires an active logger with GPS selected"
    }

    fun onPhoneGpsError(message: String) {
        phoneGpsEnabled = false
        phoneGpsToggle.text = "START PHONE GPS ASSIST"
        phoneGpsStatus.text = "Phone GPS error · $message"
        toast(message)
    }

    fun submitPhoneLocation(location: Location) {
        if (!phoneGpsEnabled) return
        configureClient()
        val values = linkedMapOf(
            "latitude" to String.format(Locale.US, "%.8f", location.latitude),
            "longitude" to String.format(Locale.US, "%.8f", location.longitude),
            "provider" to (location.provider ?: "phone"),
            "sourceUnixTimeMs" to location.time.toString(),
        )
        if (location.hasAccuracy()) values["accuracyMeters"] =
            String.format(Locale.US, "%.2f", location.accuracy)
        if (location.hasAltitude()) values["altitudeMeters"] =
            String.format(Locale.US, "%.2f", location.altitude)
        if (location.hasSpeed()) values["speedKmph"] =
            String.format(Locale.US, "%.2f", location.speed * 3.6f)

        executor.execute {
            try {
                val logger = client.postForm("/api/heltec/fieldlog/phone-gps", values)
                activity.runOnUiThread {
                    if (!phoneGpsEnabled) return@runOnUiThread
                    loggerStatus.text = formatLogger(logger)
                    phoneGpsStatus.text = buildString {
                        append("Phone GPS active · last fix ")
                        append(String.format(Locale.US, "%.6f, %.6f", location.latitude, location.longitude))
                        if (location.hasAccuracy()) append(" · ±${location.accuracy.toInt()} m")
                    }
                }
            } catch (error: Exception) {
                activity.runOnUiThread {
                    if (phoneGpsEnabled) {
                        phoneGpsStatus.text = "Phone GPS fix not accepted · ${error.message ?: "device error"}"
                    }
                }
            }
        }
    }

    fun destroy() {
        webUiDialog?.dismiss()
        webUiDialog = null
        connectivityManager.bindProcessToNetwork(null)
        executor.shutdownNow()
    }

    fun exportFieldLog(fileName: String, destination: Uri) {
        configureClient()
        work("EXPORTING LOG…", {
            try {
                activity.contentResolver.openOutputStream(destination, "w")?.use { output ->
                    client.downloadFieldLog(fileName, output)
                } ?: throw IllegalStateException("Android could not open the selected destination")
            } catch (error: Exception) {
                runCatching { activity.contentResolver.delete(destination, null, null) }
                throw error
            }
        }) { bytes ->
            connectionStatus.text = "Exported $fileName · ${formatBytes(bytes)}"
            toast("Saved $fileName")
        }
    }

    fun onExportCancelled() {
        setGlobalStatus("BRUCENET READY")
    }

    fun onExportError(message: String) {
        connectionStatus.text = "Error: $message"
        setGlobalStatus("BRUCE ERROR")
        toast(message)
    }

    private fun login() {
        configureClient()
        work("LOGGING IN…", {
            if (!client.login(username.text.toString(), webPassword.text.toString())) {
                throw IllegalStateException("WebUI username or password was rejected")
            }
        }) {
            connectionStatus.text = "Authenticated to ${client.displayUrl()}"
            refreshAll()
        }
    }

    private fun refreshAll() {
        configureClient()
        work("REFRESHING…", {
            UnifiedRefresh(
                system = client.getJson("/api/heltec/status"),
                capabilities = client.getJson("/api/heltec/capabilities"),
                logger = client.getJson("/api/heltec/fieldlog"),
                lora = client.getJson("/api/heltec/lora"),
                wifi = client.getJson("/api/heltec/wifi"),
            )
        }) { snapshot ->
            systemStatus.text = formatSystem(snapshot.system, snapshot.capabilities)
            loggerStatus.text = formatLogger(snapshot.logger)
            loraStatus.text = formatLora(snapshot.lora)
            wifiSurveyStatus.text = formatWifiSurvey(snapshot.wifi)
            if (snapshot.logger.optBoolean("active")) {
                root.findViewById<Switch>(R.id.loggerGps).isChecked =
                    snapshot.logger.optJSONObject("gps")?.optBoolean("enabled") == true
                root.findViewById<Switch>(R.id.loggerBle).isChecked =
                    snapshot.logger.optJSONObject("ble")?.optBoolean("enabled") == true
                root.findViewById<Switch>(R.id.loggerWifi).isChecked =
                    snapshot.logger.optJSONObject("wifi")?.optBoolean("enabled") == true
                root.findViewById<Switch>(R.id.loggerResume).isChecked =
                    snapshot.logger.optBoolean("autoResume")
            }
            connectionStatus.text = "Authenticated · unified telemetry refreshed"
        }
    }

    private fun gpsAction(action: String) {
        configureClient()
        work("GPS ${action.uppercase()}…", {
            client.postForm("/api/heltec/gps", mapOf("action" to action))
        }) { systemStatus.text = formatSystem(it) }
    }

    private fun showGpsTrack() {
        configureClient()
        work("READING GPS…", { client.getJson("/api/heltec/gps/history") }) { json ->
            val points = json.optJSONArray("points") ?: JSONArray()
            val text = buildString {
                append("Points: ${points.length()} / ${json.optInt("capacity")}\n")
                append("Recording: ${yesNo(json.optBoolean("recording"))}\n\n")
                val first = (points.length() - 8).coerceAtLeast(0)
                for (index in first until points.length()) {
                    val point = points.getJSONObject(index)
                    append("#${point.optLong("sequence")}  ")
                    append(String.format(Locale.US, "%.6f, %.6f", point.optDouble("latitude"), point.optDouble("longitude")))
                    append("  ${point.optInt("satellites")} sat\n")
                }
                if (points.length() == 0) append("No recorded fixes yet.")
            }
            showTextDialog("GPS track", text)
        }
    }

    private fun startWifiScan() {
        configureClient()
        work("PASSIVE WI-FI SCAN…", {
            client.postForm("/api/heltec/wifi", mapOf("action" to "scan"))
        }) { status ->
            wifiSurveyStatus.text = formatWifiSurvey(status)
            root.postDelayed({ refreshWifiSurvey(showResultsWhenComplete = true) }, 1_500)
        }
    }

    private fun refreshWifiSurvey(showResultsWhenComplete: Boolean = false) {
        configureClient()
        work("REFRESHING WI-FI…", {
            client.getJson("/api/heltec/wifi") to client.getJson("/api/heltec/wifi/results")
        }) { (status, results) ->
            wifiSurveyStatus.text = formatWifiSurvey(status)
            if (showResultsWhenComplete && (status.optBoolean("scanning") || status.optBoolean("pending"))) {
                root.postDelayed({ refreshWifiSurvey(showResultsWhenComplete = true) }, 1_500)
            } else if (showResultsWhenComplete) {
                showWifiResultsDialog(results)
            }
        }
    }

    private fun showWifiResults() {
        configureClient()
        work("READING WI-FI RESULTS…", {
            client.getJson("/api/heltec/wifi") to client.getJson("/api/heltec/wifi/results")
        }) { (status, results) ->
            wifiSurveyStatus.text = formatWifiSurvey(status)
            showWifiResultsDialog(results)
        }
    }

    private fun showWifiResultsDialog(json: JSONObject) {
        val networks = json.optJSONArray("networks") ?: JSONArray()
        val text = buildString {
            append("Networks: ${networks.length()} / ${json.optInt("capacity")}")
            if (json.optInt("droppedNetworks") > 0) append(" · ${json.optInt("droppedNetworks")} omitted")
            append("\n\n")
            for (index in 0 until networks.length()) {
                val network = networks.getJSONObject(index)
                append(network.optString("ssid").ifBlank { "(hidden)" })
                append("\n  ${network.optString("bssid", "—")}")
                append(" · ch ${network.optInt("channel")}")
                append(" · ${network.optInt("rssiDbm")} dBm")
                append(" · ${network.optString("authentication", "UNKNOWN")}\n")
            }
            if (networks.length() == 0) append("No access points retained. Run a passive scan first.")
        }
        showTextDialog("Passive Wi-Fi survey", text)
    }

    private fun clearWifiResults() {
        configureClient()
        work("CLEARING WI-FI RESULTS…", {
            client.postForm("/api/heltec/wifi", mapOf("action" to "clear"))
        }) { status ->
            wifiSurveyStatus.text = formatWifiSurvey(status)
            toast("Passive Wi-Fi results cleared")
        }
    }

    private fun startLogger() {
        val gps = root.findViewById<Switch>(R.id.loggerGps).isChecked
        val ble = root.findViewById<Switch>(R.id.loggerBle).isChecked
        val wifi = root.findViewById<Switch>(R.id.loggerWifi).isChecked
        if (!gps && !ble && !wifi) {
            toast("Select GPS, BLE, Wi-Fi, or a combination")
            return
        }
        val values = mapOf(
            "action" to "start",
            "gps" to gps.toString(),
            "ble" to ble.toString(),
            "wifi" to wifi.toString(),
            "autoResume" to root.findViewById<Switch>(R.id.loggerResume).isChecked.toString(),
        )
        configureClient()
        work("STARTING LOG…", { client.postForm("/api/heltec/fieldlog", values) }) {
            loggerStatus.text = formatLogger(it)
        }
    }

    private fun loggerAction(action: String) {
        configureClient()
        work("LOGGER ${action.uppercase()}…", {
            client.postForm("/api/heltec/fieldlog", mapOf("action" to action))
        }) { loggerStatus.text = formatLogger(it) }
    }

    private fun showLoggerFiles() {
        configureClient()
        work("READING FILES…", { client.getJson("/api/heltec/fieldlog/files") }) { json ->
            val files = json.optJSONArray("files") ?: JSONArray()
            if (files.length() == 0) {
                showTextDialog(
                    "Field logs",
                    "No saved field logs.\n\nStorage: ${formatBytes(json.optLong("usedBytes"))} / ${formatBytes(json.optLong("totalBytes"))}",
                )
                return@work
            }

            val names = Array(files.length()) { index -> files.getJSONObject(index).optString("name") }
            val labels = Array(files.length()) { index ->
                val file = files.getJSONObject(index)
                buildString {
                    append(if (file.optBoolean("active")) "● " else "")
                    append(file.optString("name"))
                    append("  ·  ${formatBytes(file.optLong("sizeBytes"))}")
                }
            }
            AlertDialog.Builder(activity)
                .setTitle("Export field log")
                .setItems(labels) { _, index -> requestFieldLogExport(names[index]) }
                .setNegativeButton("Cancel", null)
                .show()
        }
    }

    private fun startLora() {
        val frequency = root.findViewById<EditText>(R.id.loraFrequency).text.toString().trim()
        if (frequency.toDoubleOrNull() == null) {
            toast("Enter a valid frequency in MHz")
            return
        }
        configureClient()
        work("STARTING LORA RX…", {
            client.postForm(
                "/api/heltec/lora",
                mapOf("action" to "start", "frequencyMHz" to frequency),
            )
        }) { loraStatus.text = formatLora(it) }
    }

    private fun loraAction(action: String) {
        configureClient()
        work("LORA ${action.uppercase()}…", {
            client.postForm("/api/heltec/lora", mapOf("action" to action))
        }) { loraStatus.text = formatLora(it) }
    }

    private fun showLoraHistory() {
        configureClient()
        work("READING LORA…", { client.getJson("/api/heltec/lora/history") }) { json ->
            val packets = json.optJSONArray("packets") ?: JSONArray()
            val text = buildString {
                append("Packets: ${packets.length()} / ${json.optInt("capacity")}\n\n")
                val first = (packets.length() - 12).coerceAtLeast(0)
                for (index in first until packets.length()) {
                    val packet = packets.getJSONObject(index)
                    append("#${packet.optLong("sequence")} ")
                    append("${packet.optDouble("rssiDbm", Double.NaN)} dBm  ")
                    append(packet.optString("message")).append('\n')
                }
                if (packets.length() == 0) append("No received packets.")
            }
            showTextDialog("LoRa history", text)
        }
    }

    private fun confirmLoraTransmit() {
        val payload = root.findViewById<EditText>(R.id.loraPayload).text.toString()
        if (payload.isBlank()) {
            toast("Enter a printable ASCII payload")
            return
        }
        typedConfirmation(
            title = "Transmit LoRa packet?",
            message = "This sends one ${payload.length}-byte packet at the firmware-limited +2 dBm setting. Confirm that the selected frequency and target are authorized, then type TRANSMIT.",
            expected = "TRANSMIT",
        ) {
            configureClient()
            work("TRANSMITTING…", {
                client.postForm(
                    "/api/heltec/lora/transmit",
                    mapOf("payload" to payload, "confirm" to "TRANSMIT"),
                )
            }) { loraStatus.text = formatLora(it) }
        }
    }

    private fun restartDevice() {
        configureClient()
        work("RESTARTING…", {
            client.post("/reboot", mapOf("action" to "restart", "confirm" to "RESTART"))
        }) {
            connectionStatus.text = "Restart accepted · reconnect after the device boots"
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun openWebUi() {
        configureClient()
        webUiDialog?.dismiss()
        val network = client.network
        if (network == null) {
            toast("Join BruceNet before opening the WebUI")
            return
        }
        if (!connectivityManager.bindProcessToNetwork(network)) {
            toast("Android could not bind the WebUI to BruceNet")
            return
        }

        val baseUri = Uri.parse(client.displayUrl())
        val webView = runCatching {
            WebView(activity).apply {
                setBackgroundColor(Color.rgb(7, 16, 20))
                settings.javaScriptEnabled = true
                settings.domStorageEnabled = true
                settings.allowFileAccess = false
                settings.allowContentAccess = false
                settings.builtInZoomControls = true
                settings.displayZoomControls = false
                webViewClient = object : WebViewClient() {
                    override fun shouldOverrideUrlLoading(
                        view: WebView,
                        request: WebResourceRequest,
                    ): Boolean {
                        val target = request.url
                        val local = target.scheme == baseUri.scheme &&
                            target.host == baseUri.host &&
                            target.port == baseUri.port
                        if (!local) toast("Blocked navigation outside the Bruce WebUI")
                        return !local
                    }
                }
                loadUrl(client.displayUrl())
            }
        }.getOrElse { error ->
            connectivityManager.bindProcessToNetwork(null)
            toast("Could not open the embedded WebUI: ${error.message ?: error.javaClass.simpleName}")
            return
        }

        val dialog = Dialog(activity).apply {
            requestWindowFeature(Window.FEATURE_NO_TITLE)
        }
        val title = TextView(activity).apply {
            text = "Bruce WebUI · local device"
            textSize = 20f
            setTextColor(activity.getColor(R.color.text))
            setPadding(24, 20, 24, 20)
        }
        val close = Button(activity).apply { text = "CLOSE" }
        val browser = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(16, 16, 16, 16)
            setBackgroundColor(activity.getColor(R.color.bg))
            addView(
                title,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
            addView(
                webView,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    0,
                    1f,
                ),
            )
            addView(
                close,
                LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.WRAP_CONTENT,
                ),
            )
        }
        dialog.setContentView(browser)
        dialog.setCanceledOnTouchOutside(false)
        close.setOnClickListener { dialog.dismiss() }
        webUiDialog = dialog
        dialog.setOnDismissListener {
            webView.stopLoading()
            webView.destroy()
            connectivityManager.bindProcessToNetwork(null)
            if (webUiDialog === dialog) webUiDialog = null
        }
        dialog.show()
        dialog.window?.apply {
            setBackgroundDrawable(ColorDrawable(activity.getColor(R.color.bg)))
            setLayout(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            )
        }
    }

    private fun configureClient() = client.configure(baseUrl.text.toString())

    private fun <T> work(label: String, action: () -> T, success: (T) -> Unit) {
        setGlobalStatus(label)
        executor.execute {
            try {
                val result = action()
                activity.runOnUiThread {
                    success(result)
                    setGlobalStatus("BRUCENET READY")
                }
            } catch (error: Exception) {
                activity.runOnUiThread {
                    val message = error.message ?: error.javaClass.simpleName
                    connectionStatus.text = "Error: $message"
                    toast(message)
                    setGlobalStatus("BRUCE ERROR")
                }
            }
        }
    }

    private fun formatSystem(json: JSONObject, capabilities: JSONObject = JSONObject()): String {
        val firmware = json.optJSONObject("firmware") ?: JSONObject()
        val battery = json.optJSONObject("battery") ?: JSONObject()
        val network = json.optJSONObject("network") ?: JSONObject()
        val system = json.optJSONObject("system") ?: JSONObject()
        val heap = system.optJSONObject("heap") ?: JSONObject()
        val gps = json.optJSONObject("gps") ?: JSONObject()
        val live = gps.optJSONObject("live") ?: JSONObject()
        val ble = json.optJSONObject("ble") ?: JSONObject()
        val fix = live.optBoolean("fix")
        val family = capabilities.optString("firmwareFamily", "Bruce")
        val location = if (fix && live.has("latitude")) {
            String.format(Locale.US, "%.6f, %.6f", live.optDouble("latitude"), live.optDouble("longitude"))
        } else "no current fix"
        return buildString {
            append("${json.optString("board", "Heltec V4")} · $family · ${firmware.optString("version", "—")}\n")
            append("Battery ${battery.optInt("percent")}% · ${battery.optInt("millivolts")} mV · uptime ${formatDuration(system.optLong("uptimeMs"))}\n")
            append("Wi-Fi ${network.optString("mode", "—")} · ${network.optString("ip", "—")} · ${network.optInt("connectedClients")} client(s)\n")
            append("Heap ${formatBytes(heap.optLong("freeBytes"))} free\n")
            append("GPS ${gps.optString("monitorState", "off")} · ${live.optInt("satellites")} sat · $location\n")
            append("BLE ${if (ble.optBoolean("advertising")) "advertising" else "idle"} · ${ble.optInt("connectedClients")} client(s)")
        }
    }

    private fun formatLogger(json: JSONObject): String {
        val gps = json.optJSONObject("gps") ?: JSONObject()
        val ble = json.optJSONObject("ble") ?: JSONObject()
        val wifi = json.optJSONObject("wifi") ?: JSONObject()
        val storage = json.optJSONObject("storage") ?: JSONObject()
        return buildString {
            append(if (json.optBoolean("active")) "● RECORDING" else "○ STOPPED")
            append(" · session ${json.optLong("sessionId")} / segment ${json.optInt("segment")}\n")
            append("GPS ${gps.optLong("fixes")} fixes (${gps.optLong("phoneFixes")} phone)\n")
            append("BLE ${ble.optLong("observations")} observations / ${ble.optInt("uniqueDevices")} unique · ")
            append("Wi-Fi ${wifi.optLong("observations")} observations / ${wifi.optInt("uniqueNetworks")} unique\n")
            append("${storage.optString("fileName", "no file")} · ${formatBytes(storage.optLong("sessionBytes"))}")
            json.optString("lastError").takeIf { it.isNotBlank() }?.let { append("\nError: $it") }
        }
    }

    private fun formatWifiSurvey(json: JSONObject): String = buildString {
        val busy = json.optBoolean("scanning") || json.optBoolean("pending")
        append(if (busy) "● PASSIVE SCAN ACTIVE" else "○ IDLE")
        append(" · ${json.optInt("networkCount")} / ${json.optInt("capacity")} retained\n")
        append("Completed ${json.optLong("scansCompleted")} · last ${json.optLong("lastScanDurationMs")} ms")
        if (json.optInt("droppedNetworks") > 0) append(" · ${json.optInt("droppedNetworks")} omitted")
        if (json.optBoolean("fieldLoggerEnabled")) append(" · field logging")
        json.optString("lastError").takeIf { it.isNotBlank() }?.let { append("\nError: $it") }
    }

    private fun formatLora(json: JSONObject): String = buildString {
        append("${json.optString("radio", "SX1262")} · ")
        append(if (json.optBoolean("listening")) "● RECEIVING" else "○ IDLE")
        append(String.format(Locale.US, " · %.3f MHz\n", json.optDouble("frequencyMHz")))
        append("Received ${json.optLong("packetsReceived")} · history ${json.optInt("historyCount")}/${json.optInt("historyCapacity")}")
        if (json.optBoolean("transmitAvailable")) {
            append(" · TX ${json.optInt("transmitPowerDbm")} dBm")
        }
        json.optString("lastMessage").takeIf { it.isNotBlank() }?.let {
            append("\nLast: $it")
            if (json.has("rssiDbm")) append(" · ${json.optDouble("rssiDbm")} dBm")
        }
    }

    private fun typedConfirmation(
        title: String,
        message: String,
        expected: String,
        confirmed: () -> Unit,
    ) {
        val input = EditText(activity).apply {
            hint = expected
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS
            setPadding(48, 16, 48, 16)
        }
        val dialog = AlertDialog.Builder(activity)
            .setTitle(title)
            .setMessage(message)
            .setView(input)
            .setNegativeButton("Cancel", null)
            .setPositiveButton("Confirm", null)
            .create()
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                if (input.text.toString().trim() == expected) {
                    dialog.dismiss()
                    confirmed()
                } else {
                    input.error = "Type $expected exactly"
                }
            }
        }
        dialog.show()
    }

    private fun showTextDialog(title: String, text: String) {
        AlertDialog.Builder(activity)
            .setTitle(title)
            .setMessage(text)
            .setPositiveButton("Close", null)
            .show()
    }

    private fun toast(message: String) = Toast.makeText(activity, message, Toast.LENGTH_LONG).show()

    private fun yesNo(value: Boolean) = if (value) "yes" else "no"

    private fun formatBytes(bytes: Long): String = when {
        bytes >= 1_048_576 -> String.format(Locale.US, "%.1f MB", bytes / 1_048_576.0)
        bytes >= 1_024 -> String.format(Locale.US, "%.1f KB", bytes / 1_024.0)
        else -> "$bytes B"
    }

    private fun formatDuration(milliseconds: Long): String {
        val seconds = milliseconds / 1_000
        val hours = seconds / 3_600
        val minutes = (seconds % 3_600) / 60
        return if (hours > 0) "${hours}h ${minutes}m" else "${minutes}m ${seconds % 60}s"
    }
}
