from pathlib import Path
import unittest


SOURCE = Path(__file__).with_name("interface.cpp").read_text(encoding="utf-8")
MAIN_SOURCE = (Path(__file__).parents[2] / "src" / "main.cpp").read_text(encoding="utf-8")


class OledMenuContractTest(unittest.TestCase):
    def test_matches_marauder_button_contract(self):
        for expected in (
            "constexpr uint32_t kDebounceMs = 30;",
            "constexpr uint32_t kClickWindowMs = 550;",
            "constexpr uint32_t kLongPressMs = 900;",
            '"1x next 2x back hold select"',
            "completed == 1 ? OledGesture::Single : OledGesture::Double",
        ):
            self.assertIn(expected, SOURCE)

        self.assertIn("!oledRawPressed && !oledStablePressed", SOURCE)
        self.assertIn("oledRawPressed || oledStablePressed || oledClickCount > 0", SOURCE)

    def test_exposes_only_supported_standalone_services(self):
        for expected in (
            '"Dashboard"',
            '"GPS monitor"',
            '"LoRa receiver"',
            '"Field logger"',
            '"Display timeout"',
            '"Sleep (PRG wake)"',
            '"Power down"',
        ):
            self.assertIn(expected, SOURCE)

        menu_definitions = SOURCE[SOURCE.index("constexpr OledMenuItem kRootMenu") : SOURCE.index("OledScreen oledScreen")]
        self.assertNotIn("transmit", menu_definitions.lower())

    def test_supports_all_display_timeout_choices(self):
        for seconds in (0, 15, 30, 45, 60):
            self.assertIn(f"setOledTimeout({seconds})", SOURCE)

    def test_standalone_menu_starts_independently_of_webui(self):
        self.assertIn("void heltecV4BeginStandaloneMenu()", SOURCE)
        self.assertIn("heltecV4BeginStandaloneMenu();", MAIN_SOURCE)
        self.assertLess(
            MAIN_SOURCE.index("heltecV4BeginStandaloneMenu();"),
            MAIN_SOURCE.index("xTaskCreate(\n        taskInputHandler"),
        )

    def test_button_polling_has_a_dedicated_task(self):
        self.assertIn('xTaskCreate(oledInputTask, "HeltecOledInput"', SOURCE)
        task_body = SOURCE[SOURCE.index("void oledInputTask(void *parameter) {") :]
        task_body = task_body[: task_body.index("\n}") + 2]
        self.assertIn("serviceOledUi(millis())", task_body)
        self.assertIn("vTaskDelay(pdMS_TO_TICKS(10))", task_body)

        shared_handler = SOURCE[SOURCE.index("void InputHandler(void) {") :]
        shared_handler = shared_handler[: shared_handler.index("\n}") + 2]
        self.assertNotIn("pollOledButton", shared_handler)

    def test_oled_rendering_does_not_starve_button_sampling(self):
        self.assertIn("U8G2_SSD1306_128X64_NONAME_F_HW_I2C", SOURCE)
        self.assertNotIn("U8G2_SSD1306_128X64_NONAME_F_SW_I2C", SOURCE)
        self.assertIn("Wire.begin(kOledDataPin, kOledClockPin);", SOURCE)
        self.assertIn("oled.setBusClock(400000);", SOURCE)
        self.assertNotIn("oledScreen == OledScreen::Status ? 1000 : 200", SOURCE)


if __name__ == "__main__":
    unittest.main()
