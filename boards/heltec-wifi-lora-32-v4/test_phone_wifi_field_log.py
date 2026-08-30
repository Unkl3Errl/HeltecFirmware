from pathlib import Path
import unittest


BOARD_DIR = Path(__file__).resolve().parent
FIELD_HEADER = (BOARD_DIR / "field_logger.h").read_text(encoding="utf-8")
FIELD_SOURCE = (BOARD_DIR / "field_logger.cpp").read_text(encoding="utf-8")
PROJECT_DIR = BOARD_DIR.parents[1]
WEB_SOURCE = (PROJECT_DIR / "src/core/wifi/webInterface.cpp").read_text(encoding="utf-8")
SERIAL_SOURCE = (PROJECT_DIR / "src/core/serialcmds.cpp").read_text(encoding="utf-8")


class PhoneWifiFieldLogContractTest(unittest.TestCase):
    def test_wifi_is_a_real_persisted_field_log_source(self):
        for expected in (
            "struct HeltecFieldWifiRecord",
            "bool wifiEnabled = false;",
            "uint32_t wifiObservations = 0;",
            "bool heltecFieldLoggerRecordWifi",
        ):
            self.assertIn(expected, FIELD_HEADER)

        for expected in (
            'preferences.putBool("wifi", wifiEnabled);',
            'addCommonFieldsLocked(document, "wifi");',
            'document["wifi"]["source"] = "android";',
            "kWifiObservationIntervalMs = 60 * 1000",
        ):
            self.assertIn(expected, FIELD_SOURCE)

    def test_authenticated_http_endpoint_precedes_generic_field_log_route(self):
        phone_route = 'server->on("/api/heltec/fieldlog/phone-wifi"'
        generic_route = 'server->on("/api/heltec/fieldlog", HTTP_GET'
        self.assertIn(phone_route, WEB_SOURCE)
        self.assertLess(WEB_SOURCE.index(phone_route), WEB_SOURCE.index(generic_route))

        endpoint = WEB_SOURCE[WEB_SOURCE.index(phone_route) : WEB_SOURCE.index(generic_route)]
        self.assertIn("checkUserWebAuth(request)", endpoint)
        self.assertIn("isHeltecMacAddress", endpoint)
        self.assertIn("heltecFieldLoggerRecordWifi(record)", endpoint)

    def test_usb_bridge_is_narrow_and_bidirectional(self):
        self.assertIn('constexpr char kHeltecBridgePrefix[] = "@HELTEC-BRIDGE ";', SERIAL_SOURCE)
        for action in (
            'action == "logger-start"',
            'action == "logger-stop"',
            'action == "logger-status"',
            'action == "phone-gps"',
            'action == "phone-wifi"',
        ):
            self.assertIn(action, SERIAL_SOURCE)
        self.assertIn("writeBridgeResponse", SERIAL_SOURCE)
        self.assertIn('writeBridgeError(id, "unsupported bridge action")', SERIAL_SOURCE)

    def test_radio_workers_start_atomically_with_ble_stack_reserved_first(self):
        self.assertIn("constexpr uint32_t kBleTaskStackBytes = 6144;", FIELD_SOURCE)
        start_services = FIELD_SOURCE[
            FIELD_SOURCE.index("bool startSelectedServices") :
            FIELD_SOURCE.index("void stopSelectedServices")
        ]
        self.assertLess(
            start_services.index('"HeltecFieldBLE"'),
            start_services.index("heltecV4SetGpsMonitor(true)"),
        )
        self.assertIn("if (startServices && !startSelectedServices", FIELD_SOURCE)
        self.assertIn('document["reason"] = "source_start_failed";', FIELD_SOURCE)


if __name__ == "__main__":
    unittest.main()
