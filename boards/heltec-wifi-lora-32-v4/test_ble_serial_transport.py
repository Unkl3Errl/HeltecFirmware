from pathlib import Path
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[2]
SERVICE_SOURCE = (
    PROJECT_DIR / "src/modules/ble_api/services/BLESerialService.cpp"
).read_text(encoding="utf-8")
SERIAL_COMMAND_SOURCE = (PROJECT_DIR / "src/core/serialcmds.cpp").read_text(
    encoding="utf-8"
)


class BleSerialTransportContractTest(unittest.TestCase):
    def test_notifications_are_fragmented_to_the_negotiated_payload(self):
        for expected in (
            "mtu - 3",
            "while (sent < size)",
            "std::min(chunkSize, size - sent)",
            "bleNotifyRetry(serial_char, data + sent, length)",
        ):
            self.assertIn(expected, SERVICE_SOURCE)

    def test_inbound_gatt_writes_are_accumulated_until_a_complete_line(self):
        for expected in (
            "service->receive(pCharacteristic->getValue())",
            "rxBuffer.append(value.data(), value.size())",
            "rxBuffer.find('\\n')",
            "rxBuffer.erase(0, end + 1)",
        ):
            self.assertIn(expected, SERVICE_SOURCE)

    def test_ble_reader_does_not_print_internal_debug_text_to_usb(self):
        self.assertNotIn('Serial.println("readStringUntil")', SERVICE_SOURCE)
        self.assertIn('serialDevice->println("COMMAND: " + cmd_str)', SERIAL_COMMAND_SOURCE)


if __name__ == "__main__":
    unittest.main()
