package com.unkl3errl.helteccontroller.marauder

import java.io.BufferedWriter
import java.io.File
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

data class MarauderSession(
    val fileName: String,
    val sizeBytes: Long,
    val modifiedAt: Long,
)

object MarauderLogSanitizer {
    private val JOIN_PASSWORD = Regex(
        """(?i)(\bjoin\b[^\r\n]*?\s-p\s+)(\"[^\"]*\"|'[^']*'|\S+)""",
    )
    private val NAMED_PASSWORD = Regex(
        """(?i)(\b(?:password|passphrase|clientpw)\s*[:=]\s*)(\"[^\"]*\"|'[^']*'|\S+)""",
    )

    fun redact(text: String): String = text
        .replace(JOIN_PASSWORD, "\$1[redacted]")
        .replace(NAMED_PASSWORD, "\$1[redacted]")
}

class MarauderSessionStore(
    private val directory: File,
    private val now: () -> Instant = Instant::now,
) {
    companion object {
        private val FILE_STAMP = DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss-SSS")
            .withZone(ZoneOffset.UTC)
        private val DISPLAY_SAFE = Regex("[^A-Za-z0-9._ -]")
    }

    private var activeFile: File? = null
    private var writer: BufferedWriter? = null

    @Synchronized
    fun start(): MarauderSession {
        activeFile?.let(::toSession)?.let { return it }
        check(directory.exists() || directory.mkdirs()) { "Could not create the session directory" }
        val stamp = FILE_STAMP.format(now())
        val file = uniqueFile("marauder-$stamp.txt")
        writer = file.bufferedWriter().also { output ->
            output.write("Heltec Controller Marauder USB session\n")
            output.write("Started: ${now()}\n\n")
            output.flush()
        }
        activeFile = file
        return toSession(file)
    }

    @Synchronized
    fun append(kind: String, text: String) {
        val output = writer ?: return
        val sanitized = MarauderLogSanitizer.redact(text).replace("\u0000", "")
        if (sanitized.isEmpty()) return
        output.write("[${now()}] [$kind] ")
        output.write(sanitized)
        if (!sanitized.endsWith('\n')) output.newLine()
        output.flush()
    }

    @Synchronized
    fun stop(reason: String? = null) {
        val output = writer ?: return
        reason?.let { output.write("[${now()}] [SESSION] ${MarauderLogSanitizer.redact(it)}\n") }
        output.write("Ended: ${now()}\n")
        output.flush()
        output.close()
        writer = null
        activeFile = null
    }

    @Synchronized
    fun current(): MarauderSession? = activeFile?.let(::toSession)

    @Synchronized
    fun sessions(): List<MarauderSession> {
        if (!directory.exists()) return emptyList()
        return directory.listFiles { file -> file.isFile && file.extension.equals("txt", true) }
            .orEmpty()
            .map(::toSession)
            .sortedByDescending(MarauderSession::modifiedAt)
    }

    @Synchronized
    fun read(fileName: String): String = resolve(fileName).readText()

    @Synchronized
    fun file(fileName: String): File = resolve(fileName)

    @Synchronized
    fun rename(fileName: String, requestedName: String): MarauderSession {
        check(activeFile?.name != fileName) { "Disconnect before renaming the active session" }
        val source = resolve(fileName)
        val cleaned = requestedName.trim().replace(DISPLAY_SAFE, "_").take(80).trim('.', ' ')
        require(cleaned.isNotEmpty()) { "Enter a session name" }
        val targetName = if (cleaned.endsWith(".txt", true)) cleaned else "$cleaned.txt"
        val target = uniqueFile(targetName, source)
        check(source.renameTo(target)) { "Android could not rename the session" }
        return toSession(target)
    }

    @Synchronized
    fun delete(fileName: String) {
        check(activeFile?.name != fileName) { "Disconnect before deleting the active session" }
        val target = resolve(fileName)
        check(target.delete()) { "Android could not delete the session" }
    }

    private fun resolve(fileName: String): File {
        require(File(fileName).name == fileName) { "Invalid session name" }
        val target = File(directory, fileName)
        require(target.isFile) { "Session not found" }
        return target
    }

    private fun uniqueFile(requestedName: String, source: File? = null): File {
        var candidate = File(directory, requestedName)
        if (!candidate.exists() || candidate == source) return candidate
        val base = candidate.nameWithoutExtension
        val extension = candidate.extension.ifBlank { "txt" }
        var suffix = 2
        while (candidate.exists() && candidate != source) {
            candidate = File(directory, "$base ($suffix).$extension")
            suffix += 1
        }
        return candidate
    }

    private fun toSession(file: File) = MarauderSession(file.name, file.length(), file.lastModified())
}
