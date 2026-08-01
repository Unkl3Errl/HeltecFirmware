from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_metadata import select_commit, select_version
from validate_firmware_metadata import require_c_string


class BuildMetadataTest(unittest.TestCase):
    def test_explicit_release_version_accepts_optional_v_prefix(self) -> None:
        self.assertEqual("0.2.2", select_version("v0.2.2", None, None, []))

    def test_github_release_tag_is_used(self) -> None:
        self.assertEqual(
            "1.4.0",
            select_version(None, "tag", "v1.4.0", ["android-v0.3.3"]),
        )

    def test_android_tag_does_not_become_firmware_version(self) -> None:
        self.assertEqual(
            "dev",
            select_version(None, "tag", "android-v0.3.3", ["android-v0.3.3"]),
        )

    def test_highest_exact_firmware_tag_is_deterministic(self) -> None:
        self.assertEqual(
            "0.10.0",
            select_version(None, None, None, ["v0.9.9", "v0.10.0"]),
        )

    def test_commit_is_shortened_and_normalized(self) -> None:
        self.assertEqual(
            "abcdef123456",
            select_commit(None, "ABCDEF1234567890ABCDEF", None, False),
        )

    def test_dirty_suffix_is_local_only(self) -> None:
        self.assertEqual(
            "abcdef123456-dirty",
            select_commit(None, None, "abcdef1234567890", True),
        )
        self.assertEqual(
            "abcdef123456",
            select_commit(None, "abcdef1234567890", None, True),
        )

    def test_unsafe_metadata_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            select_version("0.2.2 injected", None, None, [])
        with self.assertRaises(ValueError):
            select_commit("bad value", None, None, False)

    def test_binary_validator_requires_a_complete_c_string(self) -> None:
        require_c_string(b"prefix0.2.2\x00suffix", "0.2.2", "version")
        with self.assertRaises(ValueError):
            require_c_string(b"prefix0.2.20\x00suffix", "0.2.2", "version")


if __name__ == "__main__":
    unittest.main()
