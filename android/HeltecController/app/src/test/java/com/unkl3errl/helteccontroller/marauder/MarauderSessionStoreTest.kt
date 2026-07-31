package com.unkl3errl.helteccontroller.marauder

import java.time.Instant
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class MarauderSessionStoreTest {
    @get:Rule
    val temporaryFolder = TemporaryFolder()

    @Test fun recordsRedactsRenamesAndDeletesSessions() {
        val directory = temporaryFolder.newFolder("sessions")
        val fixedTime = Instant.parse("2026-07-31T22:30:00Z")
        val store = MarauderSessionStore(directory) { fixedTime }

        val started = store.start()
        store.append("TX", "join -a 0 -p secret-password")
        store.append("RX", "Password=another-secret\nready")
        store.stop("USB disconnected")

        assertNull(store.current())
        assertEquals(1, store.sessions().size)
        val content = store.read(started.fileName)
        assertTrue(content.contains("join -a 0 -p [redacted]"))
        assertTrue(content.contains("Password=[redacted]"))
        assertFalse(content.contains("secret-password"))
        assertFalse(content.contains("another-secret"))

        val renamed = store.rename(started.fileName, "Drive survey")
        assertEquals("Drive survey.txt", renamed.fileName)
        assertTrue(store.file(renamed.fileName).isFile)

        store.delete(renamed.fileName)
        assertTrue(store.sessions().isEmpty())
    }

    @Test fun activeSessionCannotBeRenamedOrDeleted() {
        val store = MarauderSessionStore(temporaryFolder.newFolder("active"))
        val session = store.start()

        assertTrue(runCatching { store.rename(session.fileName, "new name") }.isFailure)
        assertTrue(runCatching { store.delete(session.fileName) }.isFailure)
        store.stop()
    }
}
