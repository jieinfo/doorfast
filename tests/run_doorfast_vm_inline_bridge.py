#!/usr/bin/env python3
"""Rehearse passive recording and process failure on a disposable VM bridge."""
import json
import pathlib
import subprocess
import sys
import time

if len(sys.argv) != 2:
    raise SystemExit("usage: run_doorfast_vm_inline_bridge.py VM_SSH_HELPER")
ssh = str(pathlib.Path(sys.argv[1]).resolve())

def remote(command, *, check=True, input_text=None):
    return subprocess.run([ssh, "-o", "ConnectTimeout=5", command], check=check,
        input=input_text, capture_output=True, text=True, timeout=45)

interfaces = ("br-door-test", "up-test", "down-test", "u-end-test", "d-end-test")
namespaces = ("df-up-test", "df-down-test")
deployment = """config inline 'main'
 option enabled '1'
 option recording_enabled '1'
 option bridge 'br-door-test'
 option upstream 'up-test'
 option downstream 'down-test'
 option management 'br-lan'
 option observation 'up-test'
 option evidence_root '/mnt/doorfast'
 option recent_budget_mib '14336'
 option control_budget_mib '8192'
 option log_budget_mib '1024'
 option reserve_mib '6144'
"""
remote("test -x /usr/sbin/doorfast-recorder; command -v socat >/dev/null; "
       "grep -q '^/dev/vdb /mnt/doorfast ' /proc/mounts; "
       "test \"$(ls -ld /mnt/doorfast | cut -c1-10)\" = drwx------")
for name in interfaces:
    remote(f"test ! -e /sys/class/net/{name}")
for name in namespaces:
    remote(f"test ! -e /var/run/netns/{name}")
before = remote("ip -j route show table all; sha256sum /etc/config/network /etc/config/firewall").stdout
created = []
original_deployment = remote("cat /etc/config/doorfast-deployment").stdout
recorder_pid = None
listener_pids = []
doorfast_was_stopped = False
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
        remote(f"ip link add {host} type veth peer name {peer}; "
               f"ip link set {peer} netns {namespace}; "
               f"ip link set {host} master br-door-test; ip link set {host} up; "
               f"ip netns exec {namespace} ip link set lo up; "
               f"ip netns exec {namespace} ip link set {peer} up; "
               f"ip netns exec {namespace} ip address add {address}/24 dev {peer}")
        created.append(("link", host))
    remote("ip link set br-door-test up")
    remote("umask 077; sed -n '1,40p' > /etc/config/doorfast-deployment",
           input_text=deployment)
    report = json.loads(remote(
        "/usr/sbin/doorfast --preflight /etc/config/doorfast-deployment").stdout)
    if report.get("safe") is not True:
        raise RuntimeError(f"preflight unsafe: {report}")
    recorder_pid = remote("/usr/sbin/doorfast-recorder --config /etc/config/doorfast-deployment "
        ">/tmp/doorfast-recorder-test.log 2>&1 & echo $!").stdout.strip()
    if not recorder_pid.isdigit():
        raise RuntimeError("recorder did not return a PID")
    remote("sleep 1; kill -0 " + recorder_pid)
    remote("ip netns exec df-up-test ping -c 1 -W 2 192.0.2.2")
    listener_pids.append(remote(
        "ip netns exec df-down-test socat -u UDP4-RECVFROM:8300,reuseaddr - "
        ">/tmp/df-8300-test 2>/tmp/df-8300-test.err & echo $!").stdout.strip())
    listener_pids.append(remote(
        "ip netns exec df-up-test socat -u UDP4-RECVFROM:8303,reuseaddr - "
        ">/tmp/df-8303-test 2>/tmp/df-8303-test.err & echo $!").stdout.strip())
    remote("sleep 1")
    frame = "GVSGVS\\245\\245\\245\\245" + "\\000" * 28 + "\\003\\001\\000\\000"
    remote("ip netns exec df-up-test sh -c \"printf '" + frame +
           "' | socat -u - UDP4-DATAGRAM:192.0.2.2:8300\"")
    remote("ip netns exec df-down-test sh -c \"printf media | "
           "socat -u - UDP4-DATAGRAM:192.0.2.1:8303\"")
    deadline = time.monotonic() + 5
    while True:
        remote("kill -0 " + recorder_pid)
        status = json.loads(remote("cat /var/run/doorfast-recorder.status").stdout)
        if status.get("recent_packets", 0) >= 2 and status.get("control_packets", 0) >= 1:
            break
        if time.monotonic() >= deadline:
            break
        time.sleep(0.1)
    if status.get("recent_packets", 0) < 2 or status.get("control_packets", 0) < 1:
        diagnostics = remote(
            "ip -s link show up-test; ip -s link show down-test; "
            "wc -c /tmp/df-8300-test /tmp/df-8303-test 2>/dev/null; "
            "cat /tmp/df-8300-test.err /tmp/df-8303-test.err 2>/dev/null; "
            "cat /tmp/doorfast-recorder-test.log 2>/dev/null",
            check=False).stdout
        raise RuntimeError(f"unexpected recorder counters: {status}\n{diagnostics}")
    remote("kill -TERM " + recorder_pid + "; while kill -0 " + recorder_pid +
           " 2>/dev/null; do sleep 0.1; done; :")
    recorder_pid = None
    remote("ip netns exec df-up-test ping -c 2 -W 2 192.0.2.2")
    remote("test -n \"$(find /mnt/doorfast/recent -name 'recent-*.pcap' -size +24c)\"; "
           "test -n \"$(find /mnt/doorfast/control -name 'control-*.pcap' -size +24c)\"; "
           "test -n \"$(find /mnt/doorfast/logs -name 'events-*.jsonl' -size +0c)\"")
    remote("/etc/init.d/doorfast stop")
    doorfast_was_stopped = True
    remote("ip netns exec df-down-test ping -c 2 -W 2 192.0.2.1")
    remote("/etc/init.d/doorfast start")
    doorfast_was_stopped = False
    remote("pidof doorfast")
finally:
    for listener in listener_pids:
        if listener.isdigit():
            remote("kill " + listener, check=False)
    if recorder_pid:
        remote("kill -TERM " + recorder_pid, check=False)
    if doorfast_was_stopped:
        remote("/etc/init.d/doorfast start", check=False)
    remote("umask 077; sed -n '1,80p' > /etc/config/doorfast-deployment",
           check=False, input_text=original_deployment)
    for kind, name in reversed(created):
        remote(f"ip {kind} delete {name}", check=False)

after = remote("ip -j route show table all; sha256sum /etc/config/network /etc/config/firewall").stdout
if before != after:
    raise SystemExit("FAIL: route or network/firewall configuration changed")
print("PASS: real passive recording, control classification and metadata")
print("PASS: forwarding survived recorder stop and Doorfast stop")
print("PASS: disposable bridge removed and persistent network configuration unchanged")
