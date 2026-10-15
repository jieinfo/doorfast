"""No target traffic: exercise the real CLI and SDK artifact staging script."""
import os
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build/doorfast-time-query-probe"
SCRIPT = ROOT / "scripts/build-time-query-probe-sdk.sh"
ARGS = ["--interface", "test0", "--runtime-id", "1111111111111111", "--station-id", "gate_main",
        "--operation-id", "7", "--local-ip", "192.0.2.1", "--peer-ip", "192.0.2.2",
        "--local-address", "610201190101", "--peer-address", "320201000100"]


class TimeQueryCliTests(unittest.TestCase):
    def run_cli(self, args):
        return subprocess.run([str(BIN), *args], text=True, capture_output=True, timeout=3)

    def test_help_does_not_require_root_or_ubus(self):
        result = self.run_cli(["--help"])
        self.assertEqual(result.returncode, 0)
        self.assertIn("observe-only", result.stdout)

    def test_invalid_or_ambiguous_arguments_fail_before_capture(self):
        cases = [[], ARGS + ["--operation-id", "8"], ARGS + ["--send-vendor-time-query"] * 2,
                 ARGS + ["--payload", "00"]]
        for key, bad in [("--runtime-id", "old"), ("--station-id", "../other"),
                         ("--operation-id", "18446744073709551616"), ("--operation-id", "0"),
                         ("--operation-id", "-1"), ("--local-address", "short"),
                         ("--peer-address", "zz0201000100"), ("--peer-ip", "invalid")]:
            args = list(ARGS)
            args[args.index(key) + 1] = bad
            cases.append(args)
        for args in cases:
            with self.subTest(args=args):
                result = self.run_cli(args)
                self.assertEqual(result.returncode, 2)
                self.assertNotIn('"event":"ready"', result.stdout)

    def test_host_without_ubus_cannot_enter_live_capture(self):
        result = self.run_cli(ARGS)
        self.assertEqual(result.returncode, 1)
        self.assertIn("preflight_owner_unavailable_or_not_idle", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_cancel_reports_preserve_first_reason_and_observation_time(self):
        for mode, reason in (("--cancel-report", "owner_conflict"),
                             ("--read-failure-report", "status_read_failed"),
                             ("--signal-report", "interrupted"),
                             ("--timeout-report", "observation_timeout")):
            with self.subTest(mode=mode):
                result = subprocess.run([str(ROOT / "build/time-query-probe-io-tests"), mode],
                                        text=True, capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 0, result.stderr)
                row = json.loads(result.stdout)
                self.assertTrue(row["cancelled"])
                self.assertFalse(row["valid_treatment"])
                self.assertEqual(row["sent"], 0)
                self.assertIn("cancel_reason", row)
                self.assertEqual(row["cancel_reason"], reason)
                self.assertEqual(row["elapsed_ms"], 1234)
                self.assertEqual(row["preparation_stops"], 0)

    def test_missing_sdk_cannot_make_an_artifact(self):
        with tempfile.TemporaryDirectory(prefix="doorfast-sdk-missing-") as directory:
            output = Path(directory) / "artifacts/probe"
            result = subprocess.run(["sh", str(SCRIPT), directory, str(output)], capture_output=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_sdk_adapter_selects_target_compiler_not_host_or_package_install(self):
        with tempfile.TemporaryDirectory(prefix="doorfast-sdk-fixture-") as directory:
            sdk = Path(directory)
            compiler = sdk / "staging_dir/toolchain-fixture/bin/x86_64-openwrt-linux-musl-gcc"
            compiler.parent.mkdir(parents=True)
            (sdk / "staging_dir/target-fixture/usr/include").mkdir(parents=True)
            # This compiler double checks adapter arguments only, not an SDK build.
            compiler.write_text("#!/bin/sh\nprintf '%s\\n' \"$STAGING_DIR\" \"$@\"\nwhile [ \"$#\" -gt 0 ]; do\nif [ \"$1\" = -o ]; then shift; printf 'fixture-not-an-ELF' > \"$1\"; exit 0; fi\nshift\ndone\nexit 3\n")
            compiler.chmod(0o700)
            output = sdk / "artifacts/probe"
            result = subprocess.run(["sh", str(SCRIPT), str(sdk), str(output)], text=True, capture_output=True, timeout=3)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), b"fixture-not-an-ELF")
            args = result.stdout.splitlines()
            self.assertEqual(args[0], str(sdk / "staging_dir"))
            self.assertIn("-DDF_WITH_UBUS", args)
            # SDK headers contain GNU extensions; project sources stay strict.
            self.assertIn("-isystem", args)
            self.assertEqual(args[args.index("-isystem") + 1],
                             str(sdk / "staging_dir/target-fixture/usr/include"))
            for flag in ("-std=c17", "-Wall", "-Wextra", "-Werror", "-pedantic"):
                self.assertIn(flag, args)
            self.assertIn(str(ROOT / "tools/diagnostics/time_query_probe_main.c"), args)
            self.assertFalse(any("runtime_service.c" in a for a in args))
            self.assertFalse((sdk / "usr/sbin/doorfast").exists())


if __name__ == "__main__":
    unittest.main()
