import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


class VmMediaRunnerTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.ssh = self.root / "ssh.sh"
        self.marker = self.root / "route-snapshot-seen"
        self.firewall_counter = self.root / "firewall-counter"
        self.core_apk = self.root / "doorfast.apk"
        self.media_apk = self.root / "doorfast-media.apk"
        self.core_apk.write_bytes(b"core-apk-fixture")
        self.media_apk.write_bytes(b"media-apk-fixture")
        self.ssh.write_text(textwrap.dedent(r"""#!/bin/sh
            set -eu
            command_text=${1-}
            case "$command_text" in
                *"command -v apk"*) exit 0 ;;
                *"mktemp -d /tmp/doorfast-media-vm"*) cat >/dev/null; exit 0 ;;
                *"ubus call doorfast status"*)
                    printf '%s\n' '{"running":true,"media":{"installed":true,"available":true,"state":"idle","generation":0,"encoder_running":false}}'
                    ;;
                *"ubus call doorfast monitor_status"*)
                    if [ "${DOORFAST_FAKE_UNAVAILABLE:-0}" = 1 ]; then
                        available=false
                    else
                        available=true
                    fi
                    printf '{"installed":true,"available":%s,"state":"idle","generation":0,"encoder_running":false}\n' "$available"
                    ;;
                *"count=0; for comm in /proc/"*) printf '0\n' ;;
                *"ip -j rule show") printf '[]\n' ;;
                *"ip -j route show table all")
                    if [ "${DOORFAST_FAKE_NETWORK_CHANGE:-0}" = 1 ] && [ -e "$DOORFAST_FAKE_MARKER" ]; then
                        printf '[{"dst":"changed"}]\n'
                    else
                        : >"$DOORFAST_FAKE_MARKER"
                        printf '[]\n'
                    fi
                    ;;
                *"nft -j list ruleset")
                    count=0
                    [ ! -r "$DOORFAST_FAKE_FIREWALL_COUNTER" ] || read -r count <"$DOORFAST_FAKE_FIREWALL_COUNTER"
                    count=$((count + 1))
                    printf '%s\n' "$count" >"$DOORFAST_FAKE_FIREWALL_COUNTER"
                    printf '{"nftables":[{"counter":{"packets":%s,"bytes":%s}}]}\n' "$count" "$count"
                    ;;
                *"for table in tcp tcp6 udp udp6"*) printf 'tcp 0100007F:0050 0A\n' ;;
                *) printf 'unexpected command: %s\n' "$command_text" >&2; exit 64 ;;
            esac
        """))
        self.ssh.chmod(0o700)

    def tearDown(self):
        self.temporary.cleanup()

    def run_runner(self, **environment_overrides):
        environment = os.environ.copy()
        environment.update({
            "DOORFAST_FAKE_MARKER": str(self.marker),
            "DOORFAST_FAKE_FIREWALL_COUNTER": str(self.firewall_counter),
            **environment_overrides,
        })
        return subprocess.run(
            [
                "python3", "-B", "tests/run_doorfast_vm_media.py",
                str(self.ssh), str(self.core_apk), str(self.media_apk),
                "--output-dir", str(self.root / "output"),
            ],
            text=True,
            capture_output=True,
            env=environment,
        )

    def test_vm_summary_does_not_label_unrun_fixture_as_evidence(self):
        result = self.run_runner()

        self.assertEqual(0, result.returncode, result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual("installed-path-check", summary["acceptance"])
        self.assertEqual("not-run", summary["fixture"])
        self.assertEqual("not-run", summary["preemption"])
        self.assertNotIn("ffmpeg_after_preemption", summary)
        self.assertNotIn("rtsp_path", summary)

    def test_vm_rejects_unavailable_media_module(self):
        result = self.run_runner(DOORFAST_FAKE_UNAVAILABLE="1")

        self.assertNotEqual(0, result.returncode)
        self.assertIn("did not report the media module available", result.stderr)

    def test_vm_rejects_network_snapshot_change(self):
        result = self.run_runner(DOORFAST_FAKE_NETWORK_CHANGE="1")

        self.assertNotEqual(0, result.returncode)
        self.assertIn("network, firewall, or listener state changed", result.stderr)


if __name__ == "__main__":
    unittest.main()
