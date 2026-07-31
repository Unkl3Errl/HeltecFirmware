package com.unkl3errl.helteccontroller

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.net.Network
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.TextView
import com.unkl3errl.helteccontroller.bruce.BruceApiClient
import com.unkl3errl.helteccontroller.bruce.BruceNetworkManager
import java.io.File

class MainActivity : Activity(), BruceNetworkManager.Listener {
    companion object {
        private const val WIFI_PERMISSION_REQUEST = 2001
        private const val BRUCE_LOG_EXPORT_REQUEST = 2002
        private const val PHONE_GPS_PERMISSION_REQUEST = 2003
        private const val MARAUDER_EXPORT_REQUEST = 2004
        private const val STATE_PENDING_BRUCE_EXPORT = "pendingBruceExport"
        private const val STATE_PENDING_MARAUDER_EXPORT_PATH = "pendingMarauderExportPath"
        private const val STATE_PENDING_MARAUDER_EXPORT_NAME = "pendingMarauderExportName"
        private const val STATE_PENDING_MARAUDER_EXPORT_TYPE = "pendingMarauderExportType"
    }

    private lateinit var globalStatus: TextView
    private lateinit var container: FrameLayout
    private lateinit var tabBruce: Button
    private lateinit var tabMarauder: Button
    private lateinit var bruceView: View
    private lateinit var marauderView: View
    private lateinit var bruceController: BruceScreenController
    private lateinit var marauderController: MarauderScreenController
    private lateinit var bruceNetworkManager: BruceNetworkManager
    private lateinit var phoneLocationManager: LocationManager
    private val bruceClient = BruceApiClient()
    private var pendingWifi: Pair<String, String>? = null
    private var pendingBruceExportName: String? = null
    private var pendingMarauderExportPath: String? = null
    private var pendingMarauderExportName: String? = null
    private var pendingMarauderExportType: String? = null
    private var phoneGpsRequested = false
    private val phoneLocationListener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            bruceController.submitPhoneLocation(location)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        pendingBruceExportName = savedInstanceState?.getString(STATE_PENDING_BRUCE_EXPORT)
        pendingMarauderExportPath = savedInstanceState?.getString(STATE_PENDING_MARAUDER_EXPORT_PATH)
        pendingMarauderExportName = savedInstanceState?.getString(STATE_PENDING_MARAUDER_EXPORT_NAME)
        pendingMarauderExportType = savedInstanceState?.getString(STATE_PENDING_MARAUDER_EXPORT_TYPE)

        globalStatus = findViewById(R.id.globalStatus)
        container = findViewById(R.id.screenContainer)
        tabBruce = findViewById(R.id.tabBruce)
        tabMarauder = findViewById(R.id.tabMarauder)
        bruceNetworkManager = BruceNetworkManager(this, this)
        phoneLocationManager = getSystemService(LocationManager::class.java)

        val inflater = LayoutInflater.from(this)
        bruceView = inflater.inflate(R.layout.screen_bruce, container, false)
        marauderView = inflater.inflate(R.layout.screen_marauder, container, false)
        bruceController = BruceScreenController(
            activity = this,
            root = bruceView,
            client = bruceClient,
            requestWifi = ::requestBruceWifi,
            requestPhoneGps = ::requestPhoneGps,
            requestFieldLogExport = ::requestBruceFieldLogExport,
            setGlobalStatus = ::setGlobalStatus,
        )
        marauderController = MarauderScreenController(
            activity = this,
            root = marauderView,
            requestExport = ::requestMarauderExport,
            setGlobalStatus = ::setGlobalStatus,
        )

        tabBruce.setOnClickListener { showScreen(bruceView, true) }
        tabMarauder.setOnClickListener { showScreen(marauderView, false) }
        showScreen(bruceView, true)
    }

    override fun onDestroy() {
        stopPhoneGps()
        bruceController.destroy()
        marauderController.destroy()
        bruceNetworkManager.release()
        super.onDestroy()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        pendingBruceExportName?.let { outState.putString(STATE_PENDING_BRUCE_EXPORT, it) }
        pendingMarauderExportPath?.let {
            outState.putString(STATE_PENDING_MARAUDER_EXPORT_PATH, it)
        }
        pendingMarauderExportName?.let {
            outState.putString(STATE_PENDING_MARAUDER_EXPORT_NAME, it)
        }
        pendingMarauderExportType?.let {
            outState.putString(STATE_PENDING_MARAUDER_EXPORT_TYPE, it)
        }
        super.onSaveInstanceState(outState)
    }

    @Deprecated("Deprecated in Android; retained for API 29 document-provider compatibility")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        when (requestCode) {
            BRUCE_LOG_EXPORT_REQUEST -> {
                val fileName = pendingBruceExportName
                pendingBruceExportName = null
                val destination = data?.data
                if (resultCode == RESULT_OK && fileName != null && destination != null) {
                    bruceController.exportFieldLog(fileName, destination)
                } else {
                    bruceController.onExportCancelled()
                }
            }
            MARAUDER_EXPORT_REQUEST -> finishMarauderExport(resultCode, data?.data)
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == PHONE_GPS_PERMISSION_REQUEST) {
            val granted = checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) ==
                PackageManager.PERMISSION_GRANTED
            if (granted && phoneGpsRequested) startPhoneGps()
            else {
                phoneGpsRequested = false
                bruceController.onPhoneGpsError("Precise location permission was denied")
            }
            return
        }
        if (requestCode != WIFI_PERMISSION_REQUEST) return
        val request = pendingWifi
        pendingWifi = null
        val requiredGranted = if (Build.VERSION.SDK_INT in 31..32) {
            checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        } else {
            grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED
        }
        if (requiredGranted && request != null) {
            bruceNetworkManager.request(request.first, request.second)
        } else {
            bruceController.onNetworkError("Nearby Wi-Fi permission was denied")
        }
    }

    override fun onBruceNetworkAvailable(network: Network) {
        bruceClient.network = network
        runOnUiThread {
            setGlobalStatus("BRUCENET LINK")
            bruceController.onNetworkAvailable()
        }
    }

    override fun onBruceNetworkLost() {
        bruceClient.network = null
        runOnUiThread {
            setGlobalStatus("IDLE")
            bruceController.onNetworkLost()
        }
    }

    override fun onBruceNetworkError(message: String) {
        runOnUiThread {
            setGlobalStatus("LINK ERROR")
            bruceController.onNetworkError(message)
        }
    }

    private fun requestBruceWifi(ssid: String, password: String) {
        val permissions = when {
            Build.VERSION.SDK_INT >= 33 -> arrayOf(Manifest.permission.NEARBY_WIFI_DEVICES)
            Build.VERSION.SDK_INT >= 31 -> arrayOf(
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.ACCESS_FINE_LOCATION,
            )
            else -> arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        if (permissions.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }) {
            bruceNetworkManager.request(ssid, password)
        } else {
            pendingWifi = ssid to password
            requestPermissions(permissions, WIFI_PERMISSION_REQUEST)
        }
    }

    private fun requestPhoneGps(enabled: Boolean) {
        if (!enabled) {
            stopPhoneGps()
            return
        }
        phoneGpsRequested = true
        val permissions = arrayOf(
            Manifest.permission.ACCESS_COARSE_LOCATION,
            Manifest.permission.ACCESS_FINE_LOCATION,
        )
        if (permissions.all { checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }) {
            startPhoneGps()
        } else {
            requestPermissions(permissions, PHONE_GPS_PERMISSION_REQUEST)
        }
    }

    @SuppressLint("MissingPermission")
    private fun startPhoneGps() {
        if (!phoneGpsRequested) return
        val providers = listOf(LocationManager.GPS_PROVIDER, LocationManager.NETWORK_PROVIDER)
            .filter { provider ->
                runCatching { phoneLocationManager.isProviderEnabled(provider) }.getOrDefault(false)
            }
        if (providers.isEmpty()) {
            phoneGpsRequested = false
            bruceController.onPhoneGpsError("Enable Android location services, then try again")
            return
        }

        var registered = false
        providers.forEach { provider ->
            runCatching {
                phoneLocationManager.requestLocationUpdates(
                    provider,
                    5_000L,
                    0f,
                    phoneLocationListener,
                    mainLooper,
                )
                registered = true
            }
        }
        if (!registered) {
            phoneGpsRequested = false
            bruceController.onPhoneGpsError("Android could not start location updates")
            return
        }

        bruceController.onPhoneGpsStarted()
        providers.mapNotNull { provider ->
            runCatching { phoneLocationManager.getLastKnownLocation(provider) }.getOrNull()
        }.maxByOrNull(Location::getTime)?.let(bruceController::submitPhoneLocation)
    }

    private fun stopPhoneGps() {
        phoneGpsRequested = false
        if (::phoneLocationManager.isInitialized) {
            runCatching { phoneLocationManager.removeUpdates(phoneLocationListener) }
        }
        if (::bruceController.isInitialized) bruceController.onPhoneGpsStopped()
    }

    private fun requestBruceFieldLogExport(fileName: String) {
        pendingBruceExportName = fileName
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/x-ndjson"
            putExtra(Intent.EXTRA_TITLE, fileName)
        }
        runCatching { startActivityForResult(intent, BRUCE_LOG_EXPORT_REQUEST) }
            .onFailure {
                pendingBruceExportName = null
                bruceController.onExportError("No Android document provider is available")
            }
    }

    private fun requestMarauderExport(request: MarauderExportRequest) {
        clearPendingMarauderExport()
        val temporary = runCatching {
            File.createTempFile("marauder-export-", ".tmp", cacheDir).apply {
                writeBytes(request.content)
            }
        }.getOrElse {
            marauderController.onExportError("Could not prepare the export: ${it.message}")
            return
        }
        pendingMarauderExportPath = temporary.absolutePath
        pendingMarauderExportName = request.suggestedName
        pendingMarauderExportType = request.mimeType
        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = request.mimeType
            putExtra(Intent.EXTRA_TITLE, request.suggestedName)
        }
        runCatching { startActivityForResult(intent, MARAUDER_EXPORT_REQUEST) }
            .onFailure {
                clearPendingMarauderExport()
                marauderController.onExportError("No Android document provider is available")
            }
    }

    private fun finishMarauderExport(resultCode: Int, destination: Uri?) {
        val path = pendingMarauderExportPath
        val fileName = pendingMarauderExportName
        if (resultCode != RESULT_OK || destination == null || path == null || fileName == null) {
            clearPendingMarauderExport()
            marauderController.onExportCancelled()
            return
        }
        runCatching {
            contentResolver.openOutputStream(destination, "w")?.use { output ->
                File(path).inputStream().use { input -> input.copyTo(output) }
            } ?: throw IllegalStateException("Android could not open the selected destination")
        }.onSuccess {
            clearPendingMarauderExport()
            marauderController.onExportSaved(fileName)
        }.onFailure { error ->
            runCatching { contentResolver.delete(destination, null, null) }
            clearPendingMarauderExport()
            marauderController.onExportError("Export failed: ${error.message ?: error.javaClass.simpleName}")
        }
    }

    private fun clearPendingMarauderExport() {
        pendingMarauderExportPath?.let { path -> runCatching { File(path).delete() } }
        pendingMarauderExportPath = null
        pendingMarauderExportName = null
        pendingMarauderExportType = null
    }

    private fun showScreen(view: View, bruceSelected: Boolean) {
        container.removeAllViews()
        container.addView(view)
        val active = ColorStateList.valueOf(getColor(R.color.teal))
        val inactive = ColorStateList.valueOf(getColor(R.color.surface_high))
        tabBruce.backgroundTintList = if (bruceSelected) active else inactive
        tabMarauder.backgroundTintList = if (bruceSelected) inactive else active
        tabBruce.setTextColor(getColor(if (bruceSelected) R.color.bg else R.color.text))
        tabMarauder.setTextColor(getColor(if (bruceSelected) R.color.text else R.color.bg))
    }

    private fun setGlobalStatus(status: String) {
        if (Thread.currentThread() == mainLooper.thread) {
            globalStatus.text = status
        } else {
            runOnUiThread { globalStatus.text = status }
        }
    }
}
