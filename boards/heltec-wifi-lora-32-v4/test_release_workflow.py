from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ReleaseWorkflowContractTest(unittest.TestCase):
    def test_mobile_tags_are_isolated_from_the_all_board_release(self):
        broad_release = (ROOT / ".github/workflows/buil_parallel.yml").read_text()
        dedicated_release = (ROOT / ".github/workflows/heltec-v4.yml").read_text()

        self.assertIn('- "!v*.*.*-mobile.*"', broad_release)
        self.assertIn('tags: ["v*.*.*-mobile.*"]', dedicated_release)


if __name__ == "__main__":
    unittest.main()
