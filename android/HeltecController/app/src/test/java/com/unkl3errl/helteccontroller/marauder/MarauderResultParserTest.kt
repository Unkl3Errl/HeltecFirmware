package com.unkl3errl.helteccontroller.marauder

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MarauderResultParserTest {
    @Test fun parsesChunkedAccessPointAndBleLists() {
        val parser = MarauderResultParser()

        assertFalse(parser.consume("[0][CH:6] Home Net"))
        assertTrue(parser.consume("work -42\r\n[1][CH:11] Guest \"WiFi\" -71 (selected)\n"))
        assertFalse(parser.consume("[0][RSSI:-55] Head"))
        assertTrue(parser.consume("phones\n[1][RSSI:-88] \n"))

        assertEquals(
            listOf(
                MarauderAccessPoint(0, 6, "Home Network", -42, false),
                MarauderAccessPoint(1, 11, "Guest \"WiFi\"", -71, true),
            ),
            parser.accessPoints(),
        )
        assertEquals(
            listOf(
                MarauderBleDevice(0, "Headphones", -55),
                MarauderBleDevice(1, "(unnamed)", -88),
            ),
            parser.bleDevices(),
        )
    }

    @Test fun exportsQuotedCsvAndClearsResultsIndependently() {
        val parser = MarauderResultParser()
        parser.consume("[2][CH:1] Office, \"Lab\" -50\n[3][RSSI:-64] Sensor, A\n")

        assertEquals(
            "index,channel,ssid,rssi,selected\n2,1,\"Office, \"\"Lab\"\"\",-50,false\n",
            parser.accessPointsCsv(),
        )
        assertEquals(
            "index,name,rssi\n3,\"Sensor, A\",-64\n",
            parser.bleDevicesCsv(),
        )

        parser.clearAccessPoints()
        assertTrue(parser.accessPoints().isEmpty())
        assertEquals(1, parser.bleDevices().size)
    }
}
