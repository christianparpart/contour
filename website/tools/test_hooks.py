"""Tests for website/hooks.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import hooks  # noqa: E402

METAINFO = """<component><releases>
<release version="x.y.z" urgency="medium" type="development"/>
<release version="0.9.0" urgency="medium" type="development"/>
<release version="0.8.0" date="2026-10-23"/>
<release version="0.7.1" date="2026-01-02"/>
</releases></component>"""


class LatestReleaseTest(unittest.TestCase):
    def test_skips_placeholder_and_undated_releases(self):
        self.assertEqual(hooks.latest_release(METAINFO), "0.8.0")

    def test_no_qualifying_release(self):
        xml = '<component><releases><release version="0.9.0"/><release version="x.y.z" date="2026-01-01"/></releases></component>'
        self.assertEqual(hooks.latest_release(xml), "")

    def test_no_releases(self):
        self.assertEqual(hooks.latest_release("<component/>"), "")


if __name__ == "__main__":
    unittest.main()
