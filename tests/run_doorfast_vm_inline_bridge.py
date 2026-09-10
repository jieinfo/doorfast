#!/usr/bin/env python3
"""Disposable VM-only bridge baseline. Never operates on physical interfaces."""
import pathlib
import subprocess
import sys


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: run_doorfast_vm_inline_bridge.py VM_SSH_HELPER")
    ssh = str(pathlib.Path(sys.argv[1]).resolve())
    def remote(command):
        return subprocess.run([ssh, "-o", "ConnectTimeout=5", command],
                              check=True, capture_output=True, text=True, timeout=45).stdout
    interfaces = ("br-door-test", "up-test", "down-test", "u-end-test", "d-end-test")
    namespaces = ("df-up-test", "df-down-test")
    remote("command -v ip; test -d /sys/module/virtio_pci")
    for name in interfaces:
        remote(f"test ! -e /sys/class/net/{name}")
    for name in namespaces:
        remote(f"test ! -e /var/run/netns/{name}")
    before = remote("ip -j route show table all; sha256sum /etc/config/network /etc/config/firewall; pidof doorfast")
    created = []
    try:
        for name in namespaces:
            remote(f"ip netns add {name}")
            created.append(("netns", name))
        remote("ip link add br-door-test type bridge stp_state 0 mcast_snooping 0")
        created.append(("link", "br-door-test"))
        for host, peer, namespace, address in (
            ("up-test", "u-end-test", "df-up-test", "192.0.2.1"),
            ("down-test", "d-end-test", "df-down-test", "192.0.2.2"),
        ):
            remote(f"ip link add {host} type veth peer name {peer}")
            created.append(("link", host))
            remote(f"ip link set {peer} netns {namespace}; ip link set {host} master br-door-test; ip link set {host} up")
            remote(f"ip netns exec {namespace} ip link set lo up; ip netns exec {namespace} ip link set {peer} up; ip netns exec {namespace} ip address add {address}/24 dev {peer}")
        remote("ip link set br-door-test up")
        remote("ip netns exec df-up-test ping -c 3 -W 2 192.0.2.2")
        remote("ip netns exec df-down-test ping -c 3 -W 2 192.0.2.1")
    finally:
        errors = []
        for kind, name in reversed(created):
            try:
                remote(f"ip {kind} delete {name}")
            except subprocess.SubprocessError as error:
                errors.append(str(error))
        if errors:
            raise RuntimeError("Disposable interface cleanup failed: " + "; ".join(errors))
    after = remote("ip -j route show table all; sha256sum /etc/config/network /etc/config/firewall; pidof doorfast")
    if before != after:
        raise SystemExit("FAIL: route, configuration or Doorfast PID changed")
    print("PASS: isolated bidirectional bridge baseline; all disposable interfaces removed")
    print("Observer kill/recovery and real recording are NOT covered by this baseline.")


if __name__ == "__main__":
    main()
