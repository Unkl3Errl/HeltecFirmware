package com.unkl3errl.helteccontroller.bruce

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiNetworkSpecifier
import android.os.Handler
import android.os.Looper

class BruceNetworkManager(
    context: Context,
    private val listener: Listener,
) {
    interface Listener {
        fun onBruceNetworkAvailable(network: Network)
        fun onBruceNetworkLost()
        fun onBruceNetworkError(message: String)
    }

    private val connectivityManager =
        context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private val handler = Handler(Looper.getMainLooper())
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var requestedSsid: String? = null
    private var requestedPassword: String = ""
    private val reconnect = Runnable { requestInternal() }

    fun request(ssid: String, password: String) {
        if (ssid.isBlank()) {
            listener.onBruceNetworkError("Enter the BruceNet SSID")
            return
        }
        requestedSsid = ssid
        requestedPassword = password
        handler.removeCallbacks(reconnect)
        requestInternal()
    }

    private fun requestInternal() {
        val ssid = requestedSsid ?: return
        releaseCallback()

        val specifierBuilder = WifiNetworkSpecifier.Builder().setSsid(ssid)
        if (requestedPassword.isNotBlank()) specifierBuilder.setWpa2Passphrase(requestedPassword)

        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .setNetworkSpecifier(specifierBuilder.build())
            .build()

        val networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                if (callback !== this) return
                handler.removeCallbacks(reconnect)
                listener.onBruceNetworkAvailable(network)
            }

            override fun onLost(network: Network) {
                if (callback !== this) return
                listener.onBruceNetworkLost()
                scheduleReconnect()
            }

            override fun onUnavailable() {
                if (callback !== this) return
                listener.onBruceNetworkError("BruceNet is unavailable; retrying local link")
                scheduleReconnect()
            }
        }
        callback = networkCallback
        connectivityManager.requestNetwork(request, networkCallback)
    }

    fun release() {
        requestedSsid = null
        requestedPassword = ""
        handler.removeCallbacks(reconnect)
        releaseCallback()
    }

    private fun releaseCallback() {
        callback?.let { runCatching { connectivityManager.unregisterNetworkCallback(it) } }
        callback = null
    }

    private fun scheduleReconnect() {
        if (requestedSsid == null) return
        handler.removeCallbacks(reconnect)
        handler.postDelayed(reconnect, 2_000L)
    }
}
