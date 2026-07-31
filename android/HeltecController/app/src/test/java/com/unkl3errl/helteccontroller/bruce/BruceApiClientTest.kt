package com.unkl3errl.helteccontroller.bruce

import org.junit.Assert.assertEquals
import org.junit.Test

class BruceApiClientTest {
    @Test
    fun fieldLogDownloadPathEncodesTheFileNameAsOneQueryValue() {
        assertEquals(
            "/api/heltec/fieldlog/download?name=session-000003-000.ndjson",
            fieldLogDownloadPath("session-000003-000.ndjson"),
        )
        assertEquals(
            "/api/heltec/fieldlog/download?name=name%26action%3Ddelete.ndjson",
            fieldLogDownloadPath("name&action=delete.ndjson"),
        )
    }
}
