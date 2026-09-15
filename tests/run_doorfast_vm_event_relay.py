#!/usr/bin/env python3
"""Exercise the packaged Doorfast event relay against a temporary HTTPS receiver."""

import argparse
import http.server
import ipaddress
import json
import pathlib
import secrets
import ssl
import subprocess
import sys
import tempfile
import threading


def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, **kwargs)


parser = argparse.ArgumentParser()
parser.add_argument("ssh", type=pathlib.Path)
parser.add_argument("--host-address", default="10.0.2.2")
parser.add_argument("--port", type=int, default=8443)
args = parser.parse_args()
ssh = str(args.ssh.resolve())
try:
    host_address = str(ipaddress.IPv4Address(args.host_address))
except ipaddress.AddressValueError as error:
    raise SystemExit("--host-address must be an IPv4 address") from error
suffix = secrets.token_hex(6)
remote_token = f"/etc/doorfast/.vm-relay-{suffix}-token"
remote_ca = f"/etc/doorfast/.vm-relay-{suffix}-ca.crt"
remote_socket = f"/tmp/doorfast-relay-{suffix}.sock"
remote_log = f"/tmp/doorfast-relay-{suffix}.log"


def remote(command, *, input_text=None):
    return run([ssh, command], input=input_text, stdout=subprocess.PIPE).stdout


def network_state():
    volatile = {"packets", "bytes", "expires", "used", "age"}

    def stable(value):
        if isinstance(value, dict):
            return {key: stable(item) for key, item in value.items() if key not in volatile}
        if isinstance(value, list):
            return [stable(item) for item in value]
        return value

    firewall = remote("nft -j list ruleset").strip()
    return (
        remote("uci export network"),
        remote("uci export firewall"),
        json.dumps(stable(json.loads(firewall)), sort_keys=True) if firewall else "",
    )


before = network_state()
with tempfile.TemporaryDirectory(prefix="doorfast-relay-") as directory:
    root = pathlib.Path(directory)
    (root / "ca.cnf").write_text(
        "[req]\ndistinguished_name=dn\nprompt=no\nx509_extensions=v3\n"
        "[dn]\nCN=Doorfast VM Test CA\n[v3]\nbasicConstraints=critical,CA:TRUE\n"
        "keyUsage=critical,keyCertSign,cRLSign\n"
    )
    (root / "server.cnf").write_text(
        f"[req]\ndistinguished_name=dn\nprompt=no\nreq_extensions=req_ext\n"
        f"[dn]\nCN={host_address}\n[req_ext]\nsubjectAltName=IP:{host_address}\n"
        f"[server]\nsubjectAltName=IP:{host_address}\nextendedKeyUsage=serverAuth\n"
    )
    run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
         "-keyout", root / "ca.key", "-out", root / "ca.crt", "-config", root / "ca.cnf"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(["openssl", "req", "-newkey", "rsa:2048", "-nodes", "-keyout", root / "server.key",
         "-out", root / "server.csr", "-config", root / "server.cnf"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(["openssl", "x509", "-req", "-days", "1", "-in", root / "server.csr",
         "-CA", root / "ca.crt", "-CAkey", root / "ca.key", "-CAcreateserial",
         "-out", root / "server.crt", "-extfile", root / "server.cnf", "-extensions", "server"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    received = []
    attempts = {}
    attempt_bodies = {}

    class Receiver(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            event_id = body["event_id"]
            attempts[event_id] = attempts.get(event_id, 0) + 1
            attempt_bodies.setdefault(event_id, []).append(body)
            if event_id == 102 and attempts[event_id] == 1:
                self.connection.close()
                return
            received.append((self.path, self.headers.get("Authorization"), body))
            self.send_response(200)
            self.end_headers()

        def log_message(self, *_args):
            pass

    server = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), Receiver)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(root / "server.crt", root / "server.key")
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        credential_dir = remote("ls -ldn /etc/doorfast").split()
        if len(credential_dir) < 4 or credential_dir[0] != "drwxr-x---" or credential_dir[2:4] != ["0", "0"]:
            raise SystemExit(f"unexpected packaged credential directory: {credential_dir!r}")
        remote(f"set -eu; [ ! -e '{remote_token}' ]; [ ! -e '{remote_ca}' ]; [ ! -e '{remote_socket}' ]; [ ! -e '{remote_log}' ]")
        remote(f"cat > '{remote_ca}'",
               input_text=(root / "ca.crt").read_text())
        remote(f"printf vm-test-token > '{remote_token}'; chmod 0600 '{remote_token}'; chmod 0644 '{remote_ca}'")
        remote(f"set -eu; . /etc/init.d/doorfast-event-relay; token_file_secure '{remote_token}'; "
               f"chmod 0640 '{remote_token}'; if token_file_secure '{remote_token}'; then exit 1; fi; "
               f"chmod 0600 '{remote_token}'; token_file_secure '{remote_token}'")
        if remote("cat /lib/apk/packages/doorfast.rusers").strip() != ":doorfast":
            raise SystemExit("doorfast APK is missing the group-only rusers declaration")
        group_fields = remote("grep '^doorfast:' /etc/group").strip().split(":")
        if len(group_fields) != 4 or not group_fields[2].isdigit():
            raise SystemExit(f"invalid doorfast group: {group_fields!r}")
        doorfast_gid = group_fields[2]
        runtime_fields = remote("ls -ldn /var/run/doorfast").split()
        socket_fields = remote("ls -ldn /var/run/doorfast/events.sock").split()
        if len(runtime_fields) < 4 or runtime_fields[0] != "drwxr-x---" or runtime_fields[2:4] != ["0", doorfast_gid]:
            raise SystemExit(f"unexpected event directory identity: {runtime_fields!r}")
        if socket_fields[0] != "srw-rw----" or socket_fields[2:4] != ["0", doorfast_gid]:
            raise SystemExit(f"unexpected event socket identity: {socket_fields!r}")
        command = f"""
        set -eu
        relay_pid=
        socat_pid=
        cleanup() {{
          [ -z "$relay_pid" ] || kill "$relay_pid" 2>/dev/null || true
          [ -z "$socat_pid" ] || kill "$socat_pid" 2>/dev/null || true
        }}
        trap cleanup EXIT INT TERM
        (printf '%s\\n' \
          '{{"schema_version":1,"event_id":101,"event":"incoming_call","generation":9,"timestamp_ms":1001}}' \
          '{{"schema_version":1,"event_id":102,"event":"hangup","generation":9,"timestamp_ms":1002}}'; sleep 3) |
          socat - UNIX-LISTEN:{remote_socket} & socat_pid=$!
        sleep 1
        /usr/sbin/doorfast-event-relay --socket {remote_socket} \
          --url https://{host_address}:{args.port} --entry-id vm-entry \
          --token-file {remote_token} --ca-file {remote_ca} \
          >{remote_log} 2>&1 & relay_pid=$!
        sleep 8
        wait "$socat_pid" 2>/dev/null || true
        socat_pid=
        kill $relay_pid 2>/dev/null || true
        wait $relay_pid 2>/dev/null || true
        relay_pid=
        cat {remote_log}
        """
        remote(command)
    finally:
        active_error = sys.exc_info()[0] is not None
        server.shutdown()
        server.server_close()
        cleanup = subprocess.run(
            [ssh, f"rm -f '{remote_token}' '{remote_ca}' '{remote_socket}' '{remote_log}'"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if cleanup.returncode != 0:
            if not active_error:
                raise RuntimeError(f"VM relay cleanup failed: {cleanup.stderr.strip()}")
            print(f"VM relay cleanup also failed: {cleanup.stderr.strip()}", file=sys.stderr)

expected = {
    101: {"schema_version": 1, "event_id": 101, "event": "incoming_call", "generation": 9, "timestamp_ms": 1001},
    102: {"schema_version": 1, "event_id": 102, "event": "hangup", "generation": 9, "timestamp_ms": 1002},
}
if len(received) != 2 or attempts.get(102, 0) < 2 or attempt_bodies.get(102) != [expected[102], expected[102]]:
    raise SystemExit(f"relay delivery/retry mismatch: received={received!r} attempts={attempts!r}")
for path, authorization, body in received:
    if path != "/api/doorfast/vm-entry" or authorization != "Bearer vm-test-token" or body != expected[body["event_id"]]:
        raise SystemExit(f"relay request mismatch: {(path, authorization, body)!r}")
if network_state() != before:
    raise SystemExit("relay acceptance changed VM network or firewall configuration")
print("Doorfast VM relay HTTPS delivery, retry, identity, and network invariants passed.")
