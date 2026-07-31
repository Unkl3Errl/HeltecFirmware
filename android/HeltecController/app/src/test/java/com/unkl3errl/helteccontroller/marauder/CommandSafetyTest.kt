package com.unkl3errl.helteccontroller.marauder

import org.junit.Assert.assertEquals
import org.junit.Test

class CommandSafetyTest {
    @Test fun passiveCommandsDoNotRequireConfirmation() {
        assertEquals(CommandRisk.SAFE, CommandSafety.classify("gps -g fix"))
        assertEquals(CommandRisk.SAFE, CommandSafety.classify("sniffbt"))
        assertEquals(CommandRisk.SAFE, CommandSafety.classify("STOPSCAN"))
    }

    @Test fun transmitAndStateChangingCommandsAreActive() {
        assertEquals(CommandRisk.ACTIVE, CommandSafety.classify("attack -t deauth"))
        assertEquals(CommandRisk.ACTIVE, CommandSafety.classify("blespam -t all"))
        assertEquals(CommandRisk.ACTIVE, CommandSafety.classify("reboot"))
    }

    @Test fun unknownCommandsRequireConfirmation() {
        assertEquals(CommandRisk.CONFIRM, CommandSafety.classify("futurecommand -x"))
        assertEquals(CommandRisk.CONFIRM, CommandSafety.classify(""))
    }
}
