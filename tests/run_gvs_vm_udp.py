"""Manual isolated-VM integration check; run from repository root.

Usage: python3 -B tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh [--wait-for-takeover]
Requires the documented loopback UDP forward and test identity IS:2-1-101-2.
Restarts Doorfast in the test VM. Does not change its configuration.
"""
import json
import subprocess
import sys
import time


def main():
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and
                                      sys.argv[2] != '--wait-for-takeover'):
        raise SystemExit(f'usage: {sys.argv[0]} VM_SSH [--wait-for-takeover]')
    ssh = sys.argv[1]
    wait_for_takeover = len(sys.argv) == 3

    def remote(command):
        return subprocess.check_output([ssh, command], text=True)

    def status():
        return json.loads(remote('ubus -t 3 call doorfast status "{}"'))

    def wait_for(predicate, timeout=15, interval=0.1):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                result = predicate()
                if result:
                    return result
            except (subprocess.CalledProcessError, json.JSONDecodeError):
                pass
            time.sleep(interval)
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
    probe_log_command = ('logread | grep '
                         '"doorfast: event=peer_probe accepted=1 '
                         'reply_pending=1 peer_observed=1 mode=passive" '
                         '|| true')
    probe_logs_before = remote(probe_log_command)
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario',
                    'peer-probe'], check=True)
    wait_for(lambda: remote(probe_log_command) != probe_logs_before, timeout=5)
    wait_for(lambda: status()['sync']['online_peers'] == 1)
    coalesced_log_command = ('logread | grep '
                             '"doorfast: event=peer_probe accepted=1 '
                             'reply_pending=1 peer_observed=1 mode=passive '
                             'pending=1 coalesced=1 queue_full=0" || true')
    coalesced_logs_before = remote(coalesced_log_command)
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario',
                    'peer-probe'], check=True)
    wait_for(lambda: remote(coalesced_log_command) != coalesced_logs_before,
             timeout=5)
    expired_log_command = ('logread | grep '
                           '"doorfast: event=peer_reply_expired count=1 '
                           'pending=0 mode=passive" || true')
    expired_logs_before = remote(expired_log_command)
    wait_for(lambda: remote(expired_log_command) != expired_logs_before,
             timeout=5)
    peer_log_command = ('logread | grep '
                        '"doorfast: event=peer_reply accepted=1 mode=passive" '
                        '|| true')
    peer_logs_before = remote(peer_log_command)
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario',
                    'peer-online'], check=True)
    wait_for(lambda: remote(peer_log_command) != peer_logs_before, timeout=5)
    if status()['sync']['online_peers'] != 1:
        raise RuntimeError("Peer reply did not preserve online observation")
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario',
                    'periodic-sync'], check=True)
    wait_for(lambda: status()['sync']['last_opcode'] == 3)
    periodic = status()
    if (periodic['sync']['role'] != 'follower' or
            not periodic['sync']['last_accepted'] or
            periodic['sync']['version'] != 0):
        raise RuntimeError(f"Unexpected periodic result: {periodic}")
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario',
                    'normal-update'], check=True)
    wait_for(lambda: status()['sync']['version'] == 8)
    updated = status()
    persisted = remote("uci get doorfast-sync.sync.version").strip()
    if (updated['sync']['role'] != 'follower' or persisted != '8'):
        raise RuntimeError(
            f"Version update was not persisted: status={updated}, uci={persisted}")
    command = 'logread | grep "doorfast: event=IncomingCall" || true'
    before = remote(command)
    subprocess.run(['build/gvs-peer-udp-inject', '--scenario', 'call-local'],
                   check=True)
    wait_for(lambda: remote(command) != before, timeout=5)
    if wait_for_takeover:
        action_command = ('logread | grep '
                          '"doorfast: event=sync_action action=periodic_sync" '
                          '|| true')
        actions_before = remote(action_command)
        first_miss = wait_for(
            lambda: (current if (current := status())['sync']['role'] ==
                     'follower' and current['sync']['periodic_misses'] == 1
                     else None),
            timeout=65, interval=1)
        if status()['sync']['online_peers'] != 0:
            raise RuntimeError("Peer remained online after its 60-second deadline")
        takeover = wait_for(
            lambda: (current if (current := status())['sync']['role'] ==
                     'maintainer' else None),
            timeout=65, interval=1)
        if remote(action_command) == actions_before:
            raise RuntimeError("Takeover did not emit periodic_sync action")
        print(json.dumps(first_miss))
        print(json.dumps(takeover))
    else:
        print(json.dumps(updated))
    print('PASS: VM accepted a 07/01 peer probe as reply_pending without '
          'sending, observed a 07/81 peer reply, received Period and Normal '
          'sync, persisted version 8, and emitted a new IncomingCall' +
          ('; two missed periods triggered takeover' if wait_for_takeover
           else ''))


if __name__ == '__main__':
    main()
