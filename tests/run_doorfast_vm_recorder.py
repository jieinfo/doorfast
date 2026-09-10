#!/usr/bin/env python3
"""Network-free packaged recorder acceptance; never enables field recording."""
import json
import pathlib
import subprocess
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: run_doorfast_vm_recorder.py /absolute/path/to/vm/ssh.sh")
    ssh = str(pathlib.Path(sys.argv[1]).resolve())

    def remote(command):
        return subprocess.run([ssh, command], check=True, text=True,
                              capture_output=True, timeout=60).stdout

    def snapshot():
        return {
            "routes": json.loads(remote("ip -j route show table all")),
            "addresses": json.loads(remote("ip -j address show")),
            "config_hashes": remote("sha256sum /etc/config/network /etc/config/firewall /etc/config/dhcp /etc/config/doorfast"),
            "doorfast_pid": remote("pidof doorfast").strip(),
        }

    before = snapshot()
    if not before["doorfast_pid"]:
        raise SystemExit("Doorfast must already be running for independence verification")
    output = remote(
        "set -eu; test -x /usr/sbin/doorfast-recorder; "
        "test ! -e /tmp/doorfast-recorder-selftest; "
        "mkdir -m 700 /tmp/doorfast-recorder-selftest; "
        "trap 'rmdir /tmp/doorfast-recorder-selftest' EXIT; "
        "/usr/sbin/doorfast-recorder --self-test /tmp/doorfast-recorder-selftest"
    )
    if "PASS: recorder rotation, reserve guard, reopen" not in output:
        raise SystemExit("Missing successful self-test result")
    after = snapshot()
    # Address lifetimes naturally tick down; compare stable identity and scope.
    for state in (before, after):
        for interface in state["addresses"]:
            for address in interface.get("addr_info", []):
                address.pop("valid_life_time", None)
                address.pop("preferred_life_time", None)
    if before != after:
        raise SystemExit("FAIL: network configuration or main daemon changed")
    print("PASS: packaged recorder self-test; main PID and network state preserved")
    print("Live bridge recording and kill/recovery remain separate acceptance gates.")


if __name__ == "__main__":
    main()
