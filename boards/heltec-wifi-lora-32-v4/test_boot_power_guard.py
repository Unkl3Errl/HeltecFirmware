from pathlib import Path
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[2]
SOURCE = (PROJECT_DIR / "src/main.cpp").read_text(encoding="utf-8")


class BootPowerGuardContractTest(unittest.TestCase):
    def test_low_voltage_resets_delay_peripheral_startup(self):
        for expected in (
            "if (reason == ESP_RST_BROWNOUT) guardMs = 2500;",
            "else if (reason == ESP_RST_POWERON) guardMs = 1200;",
            "digitalWrite(kBootGuardVextPin, HIGH);",
            "delay(guardMs);",
        ):
            self.assertIn(expected, SOURCE)

    def test_optional_gps_is_held_inactive(self):
        self.assertIn("digitalWrite(GPS_POWER_PIN, GPS_POWER_ACTIVE == LOW ? HIGH : LOW);", SOURCE)

    def test_guard_runs_before_memory_and_peripheral_initialization(self):
        guard_call = SOURCE.index("runBootPowerGuard();")
        psram_start = SOURCE.index("bool psramStarted = psramInit();")
        self.assertLess(guard_call, psram_start)

    def test_boot_diagnostic_reports_reset_reason_and_delay(self):
        self.assertIn('"[BOOT] reset=%s (%d), power guard=%lu ms\\n"', SOURCE)


if __name__ == "__main__":
    unittest.main()
