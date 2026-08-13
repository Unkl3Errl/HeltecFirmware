from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_metadata import select_commit, select_version
from validate_firmware_metadata import require_c_string


class BuildMetadataTest(unittest.TestCase):
    def test_explicit_mobile_release_accepts_optional_v_prefix(self) -> None:
        self.assertEqual(
            "1.16.1-mobile.1",
            select_version("v1.16.1-mobile.1", None, None, []),
        )

    def test_github_mobile_release_tag_is_used(self) -> None:
        self.assertEqual(
            "1.16.1-mobile.1",
            select_version(None, "tag", "v1.16.1-mobile.1", []),
        )

    def test_unrelated_tag_does_not_become_firmware_version(self) -> None:
        self.assertEqual(
            "dev",
            select_version(None, "tag", "android-v0.12.0", ["android-v0.12.0"]),
        )

    def test_highest_exact_firmware_tag_is_deterministic(self) -> None:
        self.assertEqual(
            "1.16.1-mobile.10",
            select_version(
                None,
                None,
                None,
                ["v1.16.1-mobile.9", "v1.16.1-mobile.10"],
            ),
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
            select_version("1.16.1 injected", None, None, [])
        with self.assertRaises(ValueError):
            select_commit("bad value", None, None, False)

    def test_binary_validator_requires_a_complete_c_string(self) -> None:
        require_c_string(b"prefix1.16.1-mobile.1\x00suffix", "1.16.1-mobile.1", "version")
        with self.assertRaises(ValueError):
            require_c_string(b"prefix1.16.1-mobile.10\x00suffix", "1.16.1-mobile.1", "version")


if __name__ == "__main__":
    unittest.main()
