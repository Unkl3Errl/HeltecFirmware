package com.unkl3errl.helteccontroller.bruce

import android.net.Network
import org.json.JSONObject
import java.io.BufferedReader
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.nio.charset.StandardCharsets

data class HttpResult(val status: Int, val body: String, val location: String? = null)

class BruceApiException(
    val status: Int,
    message: String,
) : Exception(message)

class BruceApiClient {
    @Volatile
    var network: Network? = null

    @Volatile
    private var baseUrl: String = "http://172.0.0.1"

    @Volatile
    private var sessionCookie: String? = null

    val isAuthenticated: Boolean
        get() = sessionCookie != null

    fun configure(url: String) {
        baseUrl = url.trim().trimEnd('/').ifBlank { "http://172.0.0.1" }
    }

    fun displayUrl(): String = baseUrl

    fun login(username: String, password: String): Boolean {
        sessionCookie = null
        val result = request(
            method = "POST",
            path = "/login",
            form = mapOf("username" to username, "password" to password),
            authenticated = false,
        )
        return result.status == 302 && result.location == "/" && sessionCookie != null
    }

    fun logout() {
        if (sessionCookie != null) {
            runCatching { request("GET", "/logout") }
        }
        sessionCookie = null
    }

    fun getJson(path: String): JSONObject = JSONObject(requireSuccess(request("GET", path)).body)

    fun postForm(path: String, values: Map<String, String>): JSONObject =
        JSONObject(requireSuccess(request("POST", path, values)).body)

    fun post(path: String, values: Map<String, String>): HttpResult =
        requireSuccess(request("POST", path, values))

    fun downloadFieldLog(fileName: String, destination: OutputStream): Long {
        val url = URL(baseUrl + fieldLogDownloadPath(fileName))
        val connection = ((network?.openConnection(url) ?: url.openConnection()) as HttpURLConnection)
        connection.instanceFollowRedirects = false
        connection.connectTimeout = 5_000
        connection.readTimeout = 30_000
        connection.requestMethod = "GET"
        connection.setRequestProperty("Accept", "application/x-ndjson")
        sessionCookie?.let { connection.setRequestProperty("Cookie", it) }

        return try {
            val status = connection.responseCode
            if (status !in 200..299) {
                val body = connection.errorStream
                    ?.bufferedReader()
                    ?.use(BufferedReader::readText)
                    .orEmpty()
                if (status == 401) sessionCookie = null
                throw BruceApiException(status, errorDetail(status, body))
            }

            var copied = 0L
            val buffer = ByteArray(DEFAULT_BUFFER_SIZE)
            connection.inputStream.use { input ->
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    destination.write(buffer, 0, count)
                    copied += count
                }
            }
            destination.flush()
            copied
        } finally {
            connection.disconnect()
        }
    }

    private fun requireSuccess(result: HttpResult): HttpResult {
        if (result.status in 200..299) return result
        if (result.status == 401) sessionCookie = null
        throw BruceApiException(result.status, errorDetail(result.status, result.body))
    }

    private fun request(
        method: String,
        path: String,
        form: Map<String, String>? = null,
        authenticated: Boolean = true,
    ): HttpResult {
        val url = URL(baseUrl + path)
        val connection = ((network?.openConnection(url) ?: url.openConnection()) as HttpURLConnection)
        connection.instanceFollowRedirects = false
        connection.connectTimeout = 5_000
        connection.readTimeout = 7_000
        connection.requestMethod = method
        connection.setRequestProperty("Accept", "application/json")
        if (authenticated) sessionCookie?.let { connection.setRequestProperty("Cookie", it) }

        if (form != null) {
            val encoded = form.entries.joinToString("&") { (key, value) ->
                "${encode(key)}=${encode(value)}"
            }.toByteArray(StandardCharsets.UTF_8)
            connection.doOutput = true
            connection.setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            connection.setFixedLengthStreamingMode(encoded.size)
            connection.outputStream.use { it.write(encoded) }
        }

        return try {
            val status = connection.responseCode
            val setCookie = connection.getHeaderField("Set-Cookie")
            if (!setCookie.isNullOrBlank()) {
                val cookie = setCookie.substringBefore(';')
                if (cookie.startsWith("BRUCESESSION=") && cookie != "BRUCESESSION=0") {
                    sessionCookie = cookie
                }
            }
            val stream = if (status >= 400) connection.errorStream else connection.inputStream
            val body = stream?.bufferedReader()?.use(BufferedReader::readText).orEmpty()
            HttpResult(status, body, connection.getHeaderField("Location"))
        } finally {
            connection.disconnect()
        }
    }

    private fun encode(value: String): String =
        URLEncoder.encode(value, StandardCharsets.UTF_8.name())
}

internal fun fieldLogDownloadPath(fileName: String): String =
    "/api/heltec/fieldlog/download?name=" +
        URLEncoder.encode(fileName, StandardCharsets.UTF_8.name())

private fun errorDetail(status: Int, body: String): String =
    runCatching { JSONObject(body).optString("error") }
        .getOrNull()
        ?.takeIf { it.isNotBlank() }
        ?: body.take(180).ifBlank { "HTTP $status" }
