from pathlib import Path
import unittest


PROJECT_DIR = Path(__file__).resolve().parents[2]
SD_SOURCE = (PROJECT_DIR / "src/core/sd_functions.cpp").read_text(encoding="utf-8")
STORAGE_SOURCE = (PROJECT_DIR / "src/core/android_storage.cpp").read_text(encoding="utf-8")


class AndroidStorageMountContractTest(unittest.TestCase):
    def test_formats_within_the_initial_mount_call(self):
        self.assertIn('SD.begin(true, "/android", 10, "android")', SD_SOURCE)
        self.assertNotIn("androidStoragePartitionIsBlank", SD_SOURCE)
        self.assertNotIn("SD.format(", SD_SOURCE)

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


if __name__ == "__main__":
    unittest.main()
