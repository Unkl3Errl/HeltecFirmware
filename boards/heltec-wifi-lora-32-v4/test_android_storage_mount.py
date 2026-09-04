from pathlib import Path
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[2]
SD_SOURCE = (PROJECT_DIR / "src/core/sd_functions.cpp").read_text(encoding="utf-8")
STORAGE_SOURCE = (PROJECT_DIR / "src/core/android_storage.cpp").read_text(encoding="utf-8")
GPS_SOURCE = (PROJECT_DIR / "src/modules/gps/gps_tracker.cpp").read_text(encoding="utf-8")
SD_HEADER = (PROJECT_DIR / "lib/HAL/SD.h").read_text(encoding="utf-8")
PARTITIONS = (PROJECT_DIR / "custom_16Mb.csv").read_text(encoding="utf-8")
BOARD_CONFIG = (PROJECT_DIR / "boards/heltec-wifi-lora-32-v4/heltec-wifi-lora-32-v4.ini").read_text(
    encoding="utf-8"
)


class AndroidStorageMountContractTest(unittest.TestCase):
    def test_repairs_only_a_mounted_empty_zero_free_volume(self):
        self.assertIn('SD.begin(true, "/android", 10, "android")', SD_SOURCE)
        self.assertIn('SD.freeBytes() == 0 && !virtualSpoolHasPayload("/")', SD_SOURCE)
        self.assertIn("if (!node) return true", SD_SOURCE)
        self.assertIn("depth >= 8", SD_SOURCE)
        self.assertIn("SD.format(false, partitionLabel)", SD_SOURCE)
        self.assertIn('SD.begin(false, "/android", 10, "android")', SD_SOURCE)

    def test_board_sd_alias_is_the_android_virtual_spool(self):
        self.assertIn("-DHELTEC_ANDROID_STORAGE=1", BOARD_CONFIG)
        self.assertIn("#define SD FFat", SD_HEADER)
        self.assertIn('SD.begin(true, "/android", 10, "android")', SD_SOURCE)

    def test_virtual_spool_uses_the_cross_firmware_range(self):
        compact = PARTITIONS.replace(" ", "")
        self.assertIn("android,data,fat,0xCF0000,0x300000,", compact)

    def test_output_writers_hold_files_active_until_close(self):
        writers = {
            "src/modules/wifi/sniffer.cpp": "/BrucePCAP/",
            "src/modules/ble/BLE_Suite.cpp": "/BruceSniffer/",
            "src/modules/ble/ble_sniffer.cpp": "/BruceSniffer/",
            "src/modules/wifi/responder.cpp": "/BruceResponder/",
            "src/modules/rfid/tag_o_matic.cpp": "/BruceRFID/Scans/",
            "src/modules/rfid/chameleon.cpp": "/BruceRFID/Scans/",
            "src/modules/rfid/emv_reader.cpp": "/BruceRFID/Scans/",
            "src/modules/others/mic.cpp": "/BruceMIC/",
        }
        for relative_path, output_root in writers.items():
            source = (PROJECT_DIR / relative_path).read_text(encoding="utf-8")
            self.assertIn(output_root, source, relative_path)
            self.assertIn("AndroidStorageActiveGuard", source, relative_path)

    def test_each_storage_command_retries_mounting(self):
        command_start = STORAGE_SOURCE.index("bool handleAndroidStorageCommand")
        command_body = STORAGE_SOURCE[command_start:]
        self.assertIn("if (!ensureStorage()) return true;", command_body)
        self.assertIn("if (setupSdCard()) return true;", STORAGE_SOURCE)

    def test_archive_release_still_requires_size_and_crc(self):
        for expected in (
            "actualSize != expectedSize",
            "actualCrc32 != expectedCrc32",
            "RemoveInactiveResult removed = removeInactiveFile(path)",
        ):
            self.assertIn(expected, STORAGE_SOURCE)

    def test_writes_are_read_back_before_success(self):
        for expected in (
            "file.flush()",
            "storageCrc32(path, durableSize, durableCrc32)",
            "durableSize != expectedSize",
            'storageError("durability_check_failed:"',
        ):
            self.assertIn(expected, STORAGE_SOURCE)
        error_position = STORAGE_SOURCE.index('storageError("durability_check_failed:"')
        success_position = STORAGE_SOURCE.index('"SD:OK:appended:"')
        self.assertLess(error_position, success_position)

    def test_gpx_is_published_only_after_the_closing_marker_is_durable(self):
        self.assertIn('const String path = "/BruceGPS/" + filename + ".part";', GPS_SOURCE)
        self.assertIn("</gpx>\\n", GPS_SOURCE)
        self.assertIn("footerWritten", GPS_SOURCE)
        self.assertIn("file.flush()", GPS_SOURCE)
        self.assertIn("rename(partPath, finalPath)", GPS_SOURCE)
        self.assertLess(GPS_SOURCE.index("</gpx>\\n"), GPS_SOURCE.index("rename(partPath, finalPath)"))

    def test_android_capacity_is_reported_separately_from_the_spool(self):
        for expected in (
            'operation == "host"',
            'SD:STATUS:backing=',
            'SD:STATUS:spool_total=',
            'SD:STATUS:spool_free=',
            'androidHostCapacityValid ? androidHostTotalBytes',
        ):
            self.assertIn(expected, STORAGE_SOURCE)


if __name__ == "__main__":
    unittest.main()
