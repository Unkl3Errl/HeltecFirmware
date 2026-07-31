package com.unkl3errl.helteccontroller.marauder

data class MarauderAccessPoint(
    val index: Int,
    val channel: Int,
    val ssid: String,
    val rssi: Int,
    val selected: Boolean,
)

data class MarauderBleDevice(
    val index: Int,
    val name: String,
    val rssi: Int,
)

class MarauderResultParser {
    companion object {
        private val ANSI_ESCAPE = Regex("\\u001B\\[[0-?]*[ -/]*[@-~]")
        private val AP_LINE = Regex(
            """^\[(\d+)]\[CH:(\d+)]\s+(.*?)\s+(-?\d+)(?:\s+\(selected\))?$""",
        )
        private val BLE_LINE = Regex("""^\[(\d+)]\[RSSI:(-?\d+)]\s*(.*)$""")
        private const val MAX_PARTIAL_LINE = 8_192
    }

    private val partialLine = StringBuilder()
    private val accessPointMap = linkedMapOf<Int, MarauderAccessPoint>()
    private val bleDeviceMap = linkedMapOf<Int, MarauderBleDevice>()

    @Synchronized
    fun consume(text: String): Boolean {
        var changed = false
        text.forEach { character ->
            if (character == '\n') {
                changed = parseLine(partialLine.toString().trimEnd('\r')) || changed
                partialLine.clear()
            } else {
                partialLine.append(character)
                if (partialLine.length > MAX_PARTIAL_LINE) partialLine.clear()
            }
        }
        return changed
    }

    @Synchronized
    fun clearAccessPoints() = accessPointMap.clear()

    @Synchronized
    fun clearBleDevices() = bleDeviceMap.clear()

    @Synchronized
    fun accessPoints(): List<MarauderAccessPoint> =
        accessPointMap.values.sortedWith(compareBy<MarauderAccessPoint> { it.channel }.thenByDescending { it.rssi })

    @Synchronized
    fun bleDevices(): List<MarauderBleDevice> =
        bleDeviceMap.values.sortedByDescending(MarauderBleDevice::rssi)

    @Synchronized
    fun accessPointsCsv(): String = buildString {
        append("index,channel,ssid,rssi,selected\n")
        accessPoints().forEach { ap ->
            append(ap.index).append(',')
            append(ap.channel).append(',')
            append(csv(ap.ssid)).append(',')
            append(ap.rssi).append(',')
            append(ap.selected).append('\n')
        }
    }

    @Synchronized
    fun bleDevicesCsv(): String = buildString {
        append("index,name,rssi\n")
        bleDevices().forEach { device ->
            append(device.index).append(',')
            append(csv(device.name)).append(',')
            append(device.rssi).append('\n')
        }
    }

    private fun parseLine(rawLine: String): Boolean {
        val line = ANSI_ESCAPE.replace(rawLine, "").trim()
        AP_LINE.matchEntire(line)?.let { match ->
            val ap = MarauderAccessPoint(
                index = match.groupValues[1].toInt(),
                channel = match.groupValues[2].toInt(),
                ssid = match.groupValues[3],
                rssi = match.groupValues[4].toInt(),
                selected = line.endsWith(" (selected)"),
            )
            accessPointMap[ap.index] = ap
            return true
        }
        BLE_LINE.matchEntire(line)?.let { match ->
            val device = MarauderBleDevice(
                index = match.groupValues[1].toInt(),
                rssi = match.groupValues[2].toInt(),
                name = match.groupValues[3].ifBlank { "(unnamed)" },
            )
            bleDeviceMap[device.index] = device
            return true
        }
        return false
    }

    private fun csv(value: String): String = "\"${value.replace("\"", "\"\"")}\""
}
