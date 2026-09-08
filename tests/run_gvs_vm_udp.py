"""Manual isolated-VM integration check; run from repository root.

Usage: python3 -B tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh
Requires the documented loopback UDP forward and test identity IS:2-1-101-2.
Restarts Doorfast in the test VM. Does not change its configuration.
"""
import json
import subprocess
import sys
import time


def main():
    ssh = sys.argv[1]

    def remote(command):
        return subprocess.check_output([ssh, command], text=True)

    def status():
        return json.loads(remote('ubus -t 3 call doorfast status "{}"'))

    def wait_for(predicate, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                result = predicate()
                if result:
                    return result
            except (subprocess.CalledProcessError, json.JSONDecodeError):
                pass
            time.sleep(0.1)
        raise RuntimeError("Timed out waiting for VM observation")

    config = remote('uci get doorfast.main.gvs_local_address').strip()
    if config != 'IS:2-1-101-2':
        raise RuntimeError("VM must use synthetic identity IS:2-1-101-2")
    if remote('uci get doorfast.main.passive_only').strip() != '1':
        raise RuntimeError("VM must remain passive")
    subprocess.run(['make', 'peer-udp-inject'], check=True)
    remote('/etc/init.d/doorfast restart')
    wait_for(lambda: status()['sync']['phase'] == 'sync_ask')
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario', 'sync-reply'],
                   check=True)
    wait_for(lambda: status()['sync']['last_accepted'])
    observed = status()
    if observed['sync']['role'] != 'follower' or observed['sync']['last_opcode'] != 129:
        raise RuntimeError(f"Unexpected sync result: {observed}")
    command = 'logread | grep "doorfast: event=IncomingCall" || true'
    before = remote(command)
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario', 'call-local'],
                   check=True)
    wait_for(lambda: remote(command) != before, timeout=5)
    print(json.dumps(observed))
    print('PASS: VM received sync reply, became follower, and emitted a new IncomingCall')


if __name__ == '__main__':
    main()
