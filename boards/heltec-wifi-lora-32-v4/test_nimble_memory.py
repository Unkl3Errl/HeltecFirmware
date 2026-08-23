from pathlib import Path
import unittest


BOARD_DIR = Path(__file__).resolve().parent
ENVIRONMENT = (BOARD_DIR / "heltec-wifi-lora-32-v4.ini").read_text(encoding="utf-8")
CONFIG = (BOARD_DIR / "nimble_psram_config.h").read_text(encoding="utf-8")


class NimbleMemoryContractTest(unittest.TestCase):
    def test_board_force_includes_its_nimble_memory_policy(self):
        self.assertIn(
            "-include boards/heltec-wifi-lora-32-v4/nimble_psram_config.h",
            ENVIRONMENT,
        )

    def test_nimble_host_uses_psram_instead_of_internal_dram(self):
        self.assertIn("#include <sdkconfig.h>", CONFIG)
        self.assertIn("#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL", CONFIG)
        self.assertIn("#undef CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL", CONFIG)
        self.assertIn("#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1", CONFIG)
        self.assertIn("#define CONFIG_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1", CONFIG)

    def test_external_allocation_requires_psram(self):
        self.assertIn("#if !defined(BOARD_HAS_PSRAM)", CONFIG)
        self.assertIn('#error "The Heltec V4 NimBLE configuration requires PSRAM"', CONFIG)


if __name__ == "__main__":
    unittest.main()
