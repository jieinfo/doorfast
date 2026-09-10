#!/usr/bin/env python3
"""Verify the packaged preflight command without changing VM networking."""

import json
import pathlib
import subprocess
import sys


if len(sys.argv) != 2:
    raise SystemExit("usage: run_doorfast_vm_preflight.py /absolute/path/to/vm/ssh.sh")

ssh = str(pathlib.Path(sys.argv[1]).resolve())


def remote(command, *, check=True, input_text=None):
    return subprocess.run(
        [ssh, command],
        check=check,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def state():
    commands = {
        "links": "ip -details -json link show",
        "addresses": "ip -json address show",
        "routes": "ip -json route show table all",
        "network_uci": "uci export network",
        "firewall": "nft -j list ruleset 2>/dev/null || true",
    }
    volatile = {
        "packets", "bytes", "expires", "used", "age",
        "valid_life_time", "preferred_life_time", "cacheinfo",
        "gc_timer", "hello_timer", "tcn_timer", "topology_change_timer",
        "forward_delay_timer", "hold_timer", "message_age_timer",
    }

    def stable(value):
        if isinstance(value, dict):
            return {key: stable(item) for key, item in value.items() if key not in volatile}
        if isinstance(value, list):
            return [stable(item) for item in value]
        return value

    result = {}
    for name, command in commands.items():
        output = remote(command).stdout
        if name == "network_uci" or not output.strip():
            result[name] = output
        else:
            result[name] = json.dumps(stable(json.loads(output)), sort_keys=True)
    return result


deployment = """config inline 'main'
 option enabled '1'
 option recording_enabled '0'
 option bridge 'br-door'
 option upstream 'door-up'
 option downstream 'door-down'
 option management 'br-lan'
 option evidence_root '/mnt/doorfast'
 option recent_budget_mib '14336'
 option control_budget_mib '8192'
 option log_budget_mib '1024'
 option reserve_mib '6144'
"""

service_before = remote("/etc/init.d/doorfast status", check=False)
if service_before.returncode != 0:
    raise SystemExit("Doorfast service was not running before VM preflight")
before = state()
result = remote(
    "umask 077; trap 'rm -f /tmp/doorfast-preflight-test.conf' EXIT; "
    "sed -n '1,40p' > /tmp/doorfast-preflight-test.conf; "
    "/usr/sbin/doorfast --preflight /tmp/doorfast-preflight-test.conf",
    check=False,
    input_text=deployment,
)
if result.returncode != 2:
    raise SystemExit(
        f"expected unsafe exit 2, got {result.returncode}: {result.stderr.strip()}"
    )
try:
    report = json.loads(result.stdout)
except json.JSONDecodeError as error:
    raise SystemExit(f"preflight did not return JSON: {error}") from error
if report.get("safe") is not False or "missing_interface" not in report.get("failures", []):
    raise SystemExit(f"unexpected preflight report: {report!r}")
after = state()
if before != after:
    changed = ", ".join(name for name in before if before[name] != after[name])
    raise SystemExit(f"preflight changed VM networking state: {changed}")
service_after = remote("/etc/init.d/doorfast status", check=False)
if service_after.returncode != 0:
    raise SystemExit("Doorfast service stopped during VM preflight")

print("Doorfast VM preflight remained read-only and returned unsafe JSON as expected.")
