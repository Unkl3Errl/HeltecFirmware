package com.unkl3errl.helteccontroller.marauder

enum class CommandRisk { SAFE, CONFIRM, ACTIVE }

object CommandSafety {
    private val safeCommands = setOf(
        "help", "channel", "clearlist", "settings", "gps", "gpsdata", "nmea",
        "scanall", "sniffraw", "sniffbeacon", "sniffprobe", "sniffpwn",
        "sniffpinescan", "sniffmultissid", "sniffdeauth", "sniffpmkid",
        "sniffsae", "stopscan", "wardrive", "packetcount", "foxhunt",
        "mactrack", "sniffbt", "sniffskim", "list", "info", "select",
        "brightness", "gpstracker", "gpspoi", "wardrivepoi",
    )

    private val activeCommands = setOf(
        "attack", "blespam", "evilportal", "karma", "spoofat", "findmy",
        "join", "reboot", "update", "upload", "randapmac", "randstamac",
        "cloneapmac", "clonestamac", "add", "ssid", "pingscan", "portscan",
        "arpscan",
    )

    fun classify(command: String): CommandRisk {
        val verb = command.trim().substringBefore(' ').lowercase()
        return when {
            verb.isBlank() -> CommandRisk.CONFIRM
            verb in safeCommands -> CommandRisk.SAFE
            verb in activeCommands -> CommandRisk.ACTIVE
            else -> CommandRisk.CONFIRM
        }
    }
}
