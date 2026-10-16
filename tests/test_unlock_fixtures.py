"""Offline C serializer and real HTTP CGI acceptance; no sibling checkout."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GOLDEN = ROOT / "tests/fixtures/station_unlock_v1.json"


class UnlockFixturesTest(unittest.TestCase):
    def test_coordinator_serialization_matches_shared_golden(self):
        output = subprocess.check_output([str(ROOT / "build/doorfast-tests"),
                                          "--unlock-json-fixtures"], text=True)
        self.assertEqual(json.loads(GOLDEN.read_text()), json.loads(output))
        for forbidden in ("0011223344556677", "8899aabbccddeeff", "access_material",
                          "password", "secret", "fingerprint", "logical_address"):
            self.assertNotIn(forbidden, output.lower())

    def test_real_cgi_passes_all_eight_status_shapes_unchanged(self):
        fixtures = json.loads(GOLDEN.read_text())
        self.assertEqual(8, len(fixtures))
        with tempfile.TemporaryDirectory(prefix="doorfast-unlock-cgi-") as directory:
            ubus = Path(directory) / "ubus"
            ubus.write_text('#!/bin/sh\n[ "$*" = "call doorfast status" ] || exit 1\n'
                            'printf "%s\\n" "$TEST_STATUS_JSON"\n')
            ubus.chmod(0o700)
            for fixture in fixtures:
                with self.subTest(case=fixture["case"]):
                    env = {**os.environ, "PATH": directory + ":" + os.environ["PATH"],
                           "REQUEST_METHOD": "GET", "PATH_INFO": "/api/v1/status",
                           "TEST_STATUS_JSON": json.dumps(fixture["status"])}
                    output = subprocess.check_output(["sh", str(ROOT /
                        "package/doorfast/files/doorfast-http.sh")], env=env, text=True)
                    self.assertEqual(fixture["status"], json.loads(output.split("\n\n", 1)[1]))


if __name__ == "__main__":
    unittest.main()
