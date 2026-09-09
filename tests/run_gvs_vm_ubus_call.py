"""Manual isolated-VM check for the real Doorfast ubus call-control boundary.

Usage: python3 -B tests/run_gvs_vm_ubus_call.py /absolute/path/to/vm/ssh.sh
Requires the documented loopback UDP forward and test identity IS:2-1-101-2.
Restarts Doorfast in the test VM and uses only the in-memory sender.
"""
import json
import subprocess
import sys
import time
import urllib.request


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} VM_SSH")
    ssh = sys.argv[1]

    def remote(command):
        return subprocess.check_output([ssh, command], text=True)

    def wait_for(predicate, timeout=5, interval=0.05):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                result = predicate()
                if result:
                    return result
            except (subprocess.CalledProcessError, json.JSONDecodeError):
                pass
            time.sleep(interval)
        raise RuntimeError("Timed out waiting for VM call state")

    def http_json(payload):
        request = urllib.request.Request(
            'http://127.0.0.1:8080/ubus',
            data=json.dumps(payload).encode(),
            headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=3) as response:
            return json.load(response)

    request_id = 0

    def rpc(token, object_name, method, arguments, expected_code=0):
        nonlocal request_id
        request_id += 1
        response = http_json({
            'jsonrpc': '2.0',
            'id': request_id,
            'method': 'call',
            'params': [token, object_name, method, arguments],
        })
        result = response.get('result', [])
        if not result or result[0] != expected_code:
            raise RuntimeError(
                f"Unexpected RPC result for {object_name}.{method}: {response}")
        return result[1] if len(result) > 1 else None

    if remote('uci get doorfast.main.passive_only').strip() != '1':
        raise RuntimeError("VM must remain passive")
    subprocess.run(['make', 'peer-udp-inject'], check=True)
    remote('/etc/init.d/doorfast restart')
    wait_for(lambda: remote('ubus list doorfast').strip() == 'doorfast',
             timeout=10)

    login = rpc('0' * 32, 'session', 'login', {
        'username': 'root',
        'password': '',
    })
    token = login['ubus_rpc_session']

    for method, allowed in (('status', True), ('answer', True),
                            ('hangup', True), ('delete', False)):
        access = rpc(token, 'session', 'access', {
            'scope': 'ubus',
            'object': 'doorfast',
            'function': method,
        })
        if access != {'access': allowed}:
            raise RuntimeError(f"Unexpected ACL for doorfast.{method}: {access}")

    with urllib.request.urlopen(
            'http://127.0.0.1:8080/luci-static/resources/view/doorfast/status.js',
            timeout=3) as response:
        status_view = response.read().decode()
    if 'poll.add' not in status_view or "method: 'status'" not in status_view:
        raise RuntimeError("Installed LuCI status refresh asset is incomplete")

    def status():
        return rpc(token, 'doorfast', 'status', {})

    invalid_answer = {
        'generation': 1,
        'primary_media_port': 8303,
        'secondary_media_port': 8302,
        'duration_seconds': 120,
    }
    rpc(token, 'doorfast', 'answer', invalid_answer, expected_code=2)
    rpc(token, 'doorfast', 'answer', {}, expected_code=2)
    invalid_answer['generation'] = '1'
    rpc(token, 'doorfast', 'answer', invalid_answer, expected_code=2)
    invalid_answer['generation'] = 1
    invalid_answer['primary_media_port'] = 70000
    rpc(token, 'doorfast', 'answer', invalid_answer, expected_code=2)
    rpc(token, 'doorfast', 'hangup', {}, expected_code=2)

    subprocess.run(['build/gvs-peer-udp-inject', '--scenario', 'call-local'],
                   check=True)
    ringing = wait_for(
        lambda: (current if (current := status())['call']['session'] ==
                 'ringing' else None))
    generation = ringing['call']['generation']
    request = json.dumps({
        'generation': generation,
        'primary_media_port': 8303,
        'secondary_media_port': 8302,
        'duration_seconds': 120,
    }, separators=(',', ':'))
    reply = rpc(token, 'doorfast', 'answer', json.loads(request))
    if reply != {'queued': True, 'generation': generation}:
        raise RuntimeError(f"Unexpected answer reply: {reply}")

    answer_waiting = wait_for(
        lambda: (call if (call := status()['call'])['command'] == 'answer' and
                 call['dispatch'] == 'sent' and
                 call['confirmation'] == 'waiting' else None))
    if answer_waiting['attempts'] != 1:
        raise RuntimeError(f"Unexpected answer attempts: {answer_waiting}")
    answer_expired = wait_for(
        lambda: (call if (call := status()['call'])['confirmation'] ==
                 'expired' else None), timeout=3)
    if answer_expired['command'] != 'answer':
        raise RuntimeError(f"Unexpected expired answer: {answer_expired}")

    hangup = rpc(token, 'doorfast', 'hangup', {
        'generation': generation,
        'reason': 1,
    })
    if hangup != {'queued': True, 'generation': generation}:
        raise RuntimeError(f"Unexpected hangup reply: {hangup}")
    hangup_waiting = wait_for(
        lambda: (call if (call := status()['call'])['command'] == 'hangup' and
                 call['dispatch'] == 'sent' and
                 call['confirmation'] == 'waiting' else None))
    if hangup_waiting['attempts'] != 1:
        raise RuntimeError(f"Unexpected hangup attempts: {hangup_waiting}")
    hangup_expired = wait_for(
        lambda: (call if (call := status()['call'])['command'] == 'hangup' and
                 call['confirmation'] == 'expired' else None), timeout=3)

    print(json.dumps({
        'answer_waiting': answer_waiting,
        'answer_expired': answer_expired,
        'hangup_waiting': hangup_waiting,
        'hangup_expired': hangup_expired,
    }))
    print('PASS: real ubus registration, ACL, parameter rejection, simulated '
          'answer/hangup, and LuCI HTTP status refresh all passed')


if __name__ == '__main__':
    main()
