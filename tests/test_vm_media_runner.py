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
        self.station_scan_counter = self.root / "station-scan-counter"
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
                    printf '%s\n' '{"running":true,"runtime_id":"0123456789abcdef","media":{"installed":true,"available":true,"state":"idle","generation":0,"encoder_running":false}}'
                    ;;
                *"ubus call doorfast monitor_status"*)
                    if [ "${DOORFAST_FAKE_UNAVAILABLE:-0}" = 1 ]; then
                        available=false
                    else
                        available=true
                    fi
                    printf '{"installed":true,"available":%s,"state":"idle","generation":0,"encoder_running":false}\n' "$available"
                    ;;
                *"ubus call doorfast station_scan"*)
                    count=0
                    [ ! -r "$DOORFAST_FAKE_STATION_SCAN_COUNTER" ] || read -r count <"$DOORFAST_FAKE_STATION_SCAN_COUNTER"
                    count=$((count + 1))
                    printf '%s\n' "$count" >"$DOORFAST_FAKE_STATION_SCAN_COUNTER"
                    printf '%s\n' '{"runtime_id":"0123456789abcdef","scheduled":true,"frames_sent":3}'
                    ;;
                *"ubus call doorfast station_candidates"*)
                    printf '%s\n' '{"runtime_id":"0123456789abcdef","candidates":[{"logical_address":"32:02:01:00:02:00","ipv4":"10.2.1.20","first_seen_ms":100,"last_seen_ms":200,"reply_count":3,"configured":true},{"logical_address":"32:02:01:00:03:00","ipv4":"10.2.1.30","first_seen_ms":110,"last_seen_ms":210,"reply_count":2,"configured":true},{"logical_address":"32:02:01:00:04:00","ipv4":"10.2.1.40","first_seen_ms":120,"last_seen_ms":220,"reply_count":1,"configured":false}]}'
                    ;;
                *"ubus call doorfast stations"*)
                    printf '%s\n' '{"runtime_id":"0123456789abcdef","revision":7,"stations":[{"id":"gate_main","name":"Main Gate","logical_address":"32:02:01:00:02:00","enabled":true,"stream_name":"doorfast_gate_main","route_source":"discovered","route_fresh":true,"monitorable":true,"last_seen_ms":200},{"id":"gate_service","name":"Service Gate","logical_address":"32:02:01:00:03:00","enabled":true,"stream_name":"doorfast_gate_service","route_source":"configured","route_fresh":true,"monitorable":true,"last_seen_ms":210}]}'
                    ;;
                *"wget -qO- http://127.0.0.1/cgi-bin/doorfast/api/v1/stations"*)
                    printf '%s\n' '{"runtime_id":"0123456789abcdef","revision":7,"stations":[{"id":"gate_main","name":"Main Gate","logical_address":"32:02:01:00:02:00","enabled":true,"stream_name":"doorfast_gate_main","route_source":"discovered","route_fresh":true,"monitorable":true,"last_seen_ms":200},{"id":"gate_service","name":"Service Gate","logical_address":"32:02:01:00:03:00","enabled":true,"stream_name":"doorfast_gate_service","route_source":"configured","route_fresh":true,"monitorable":true,"last_seen_ms":210}]}'
                    ;;
                *"uci -q get doorfast.main.gvs_local_address"*) printf '%s\n' 'IS:2-1-101-1' ;;
                *"uci -q get doorfast.main.multicast_mode"*) printf '%s\n' 'auto' ;;
                *"uci -q get doorfast.main.multicast_address"*) printf '\n' ;;
                *"uci export network") printf '%s\n' "package 'network'" ;;
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
            "DOORFAST_FAKE_STATION_SCAN_COUNTER": str(self.station_scan_counter),
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

    def run_station_runner(self):
        environment = os.environ.copy()
        environment["DOORFAST_FAKE_STATION_SCAN_COUNTER"] = str(self.station_scan_counter)
        return subprocess.run(
            ["python3", "-B", "tests/run_doorfast_vm_stations.py", str(self.ssh),
             "--output-dir", str(self.root / "stations-output")],
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

    def test_vm_station_runner_validates_discovery_and_configured_routes(self):
        result = self.run_station_runner()

        self.assertEqual(0, result.returncode, result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual("vm-software-acceptance", summary["acceptance"])
        self.assertEqual(3, summary["scan_sent"])
        self.assertEqual(3, summary["candidate_count"])
        self.assertEqual(2, summary["configured_station_count"])
        self.assertEqual(7, summary["configured_revision"])
        self.assertEqual("unconfirmed", summary["physical_registration"])
        report = json.loads((self.root / "stations-output" / "stations.json").read_text())
        self.assertEqual("238.0.201.129", report["multicast"]["derived_group"])
        self.assertEqual(report["multicast"]["derived_group"], report["multicast"]["effective_group"])
        self.assertTrue(all(row["route_fresh"] for row in report["configured_stations"]))
        self.assertNotIn("name", report["configured_stations"][0])
        self.assertNotIn("stream_name", report["configured_stations"][0])
        self.assertEqual(
            {"logical_address", "ipv4", "first_seen_ms", "last_seen_ms",
             "reply_count", "configured"},
            set(report["candidates"][0]),
        )


if __name__ == "__main__":
    unittest.main()
