#!/usr/bin/env python3
"""VM acceptance: python3 tests/run_doorfast_vm_pcm_http.py SSH_WRAPPER ARTIFACT.

Requires Python 3, ubus, uci and nft already present on an idle, root-accessible
ImmortalWrt VM with both APKs installed. No packages or service/network changes
are made. The target-built acceptance artifact is copied to a private /tmp tree
and removed even on failure. --isolated ARTIFACT runs only fixture tests locally;
it is host evidence, never a substitute for the installed-path VM checks.

Receive timestamps allow 2 ms VM scheduling/clock tolerance on 20 ms pacing:
every observed interval must be >= 18 ms. A busy VM can fail this assertion;
there is no automatic retry or relaxation. Unix PCM frames contain magic and
generation, not runtime_id; runtime identity is checked in HTTP/state instead.
These tests establish transport behavior, not physical audio playback.
"""

import argparse
import fcntl
import io
import json
import os
from pathlib import Path
import re
import select
import signal
import socket
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import time

RUNTIME_A = "0123456789abcdef"
RUNTIME_B = "fedcba9876543210"
PRODUCTION_STATE = Path("/tmp/doorfast-pcm-http.state")


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def command(argv, **kwargs):
    return subprocess.run(argv, check=True, capture_output=True, timeout=10, **kwargs)


def wait_until(predicate, timeout=3):
    deadline = time.monotonic() + timeout
    while not predicate():
        check(time.monotonic() < deadline, "bounded condition wait timed out")
        time.sleep(0.005)


def parse_response(stdout, stderr, returncode):
    check(returncode == 0 and not stderr, f"CGI failed: {returncode}, {stderr!r}")
    headers, payload = stdout.split(b"\r\n\r\n", 1)
    check(b"Content-Type: application/json" in headers, "missing JSON content type")
    check(b"Cache-Control: no-store" in headers, "missing no-store header")
    code = int(headers.splitlines()[0].split()[1])
    return code, json.loads(payload)


class CGI:
    def __init__(self, argv):
        self.argv = [str(item) for item in argv]
        self.children = []

    def start(self, operation="session", runtime=RUNTIME_A, generation=42,
              token="", sequence=None, body=b"", method="POST", length=None):
        query = f"runtime={runtime}&generation={generation}"
        if sequence is not None:
            query += f"&sequence={sequence}"
        env = {"PATH": os.environ.get("PATH", "/usr/sbin:/usr/bin:/sbin:/bin"),
               "PATH_INFO": f"/api/v1/audio/{operation}", "QUERY_STRING": query,
               "REQUEST_METHOD": method, "CONTENT_LENGTH": str(len(body) if length is None else length),
               "CONTENT_TYPE": "application/octet-stream" if operation == "submit.pcm" else "",
               "HTTP_X_DOORFAST_AUDIO_SESSION": token}
        # A regular input stream guarantees EOF, including deliberately short bodies.
        with tempfile.TemporaryFile() as stream:
            stream.write(body)
            stream.seek(0)
            child = subprocess.Popen(self.argv, stdin=stream, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, env=env)
        self.children.append(child)
        return child

    def finish(self, child):
        try:
            out, err = child.communicate(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.communicate()
            raise
        return parse_response(out, err, child.returncode)

    def call(self, expected=200, error=None, **kwargs):
        code, response = self.finish(self.start(**kwargs))
        check(code == expected, f"expected HTTP {expected}, received {code}: {response}")
        if error is not None:
            check(response.get("error") == error, f"expected {error}: {response}")
        return response

    def close(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)


def status_record(runtime=RUNTIME_A, generation=42):
    return (f"runtime_id={runtime}\ncall_state=talking\ngeneration={generation}\n"
            f"audio_tx_active=1\naudio_tx_generation={generation}\n").encode()


def secure_write(path, data):
    # Authoritative status replacement is atomic; the locked state inode is never replaced.
    temporary = path.with_suffix(".new")
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)
    temporary.replace(path)


def read_state(path):
    return dict(line.split("=", 1) for line in path.read_text().splitlines())


def no_datagram(receiver):
    check(not select.select([receiver], [], [], 0.05)[0], "unexpected/replayed PCM datagram")


def isolated(binary, base="/tmp"):
    with tempfile.TemporaryDirectory(prefix="df-pcm-", dir=base) as directory:
        root = Path(directory)
        status_path, state_path, socket_path = [root / name for name in ("status", "state", "pcm.sock")]
        secure_write(status_path, status_record())
        secure_write(state_path, b"")
        client = CGI([binary, "--status-file", status_path, "--state", state_path,
                      "--socket", socket_path])
        receiver = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        receiver.bind(str(socket_path))
        socket_path.chmod(0o600)
        receiver.settimeout(3)
        try:
            # Strict provider rejection: missing/duplicate/extra keys, bad scalar
            # forms, overflow, NUL, incomplete lines, unsafe filesystem objects.
            valid = status_record()
            invalid = [valid + b"extra=1\n", valid + b"generation=42\n",
                       valid.replace(b"call_state=talking\n", b""),
                       valid.replace(b"generation=42", b"generation=+42", 1),
                       valid.replace(b"generation=42", b"generation=18446744073709551616", 1),
                       valid.replace(b"audio_tx_active=1", b"audio_tx_active=2"),
                       valid.replace(b"talking", b"talking "),
                       valid.replace(RUNTIME_A.encode(), b"bad"), valid[:-1], valid + b"\0",
                       b"x" * 256]
            for record in invalid:
                secure_write(status_path, record)
                client.call(expected=503, error="status_unavailable")
                check(state_path.read_bytes() == b"", "invalid status changed state")
            secure_write(status_path, valid)
            status_path.chmod(0o644)
            client.call(expected=503, error="status_unavailable")
            status_path.unlink()
            status_path.symlink_to(state_path)
            client.call(expected=503, error="status_unavailable")
            status_path.unlink()
            os.mkfifo(status_path, 0o600)
            client.call(expected=503, error="status_unavailable")
            status_path.unlink()
            status_path.mkdir()
            client.call(expected=503, error="status_unavailable")
            status_path.rmdir()
            client.call(expected=503, error="status_unavailable")
            secure_write(status_path, valid)
            if os.geteuid() == 0:
                os.chown(status_path, 65534, -1)
                client.call(expected=503, error="status_unavailable")
                os.chown(status_path, 0, -1)
            for argv in ([binary], [binary, "--state", state_path],
                         [binary, "--state", state_path, "--state", state_path, "--socket", socket_path],
                         [binary, "--status-file", "", "--state", state_path, "--socket", socket_path],
                         client.argv + ["--state", state_path],
                         client.argv + ["--unknown", "value"]):
                result = command([str(item) for item in argv])
                code, body = parse_response(result.stdout, result.stderr, result.returncode)
                check(code == 500 and body["error"] == "invalid_config", "acceptance arguments not strict")
            no_datagram(receiver)

            opened = client.call()
            token = opened["audio_session"]
            check(re.fullmatch("[0-9a-f]{32}", token), "invalid producer token")
            check((opened["runtime_id"], opened["generation"], opened["next_sequence"], opened["lease_ms"])
                  == (RUNTIME_A, 42, 0, 2000), "incorrect session identity/lease")
            original_deadline = int(read_state(state_path)["lease_deadline_ms"])
            client.call(expected=409, error="producer_busy")
            frames = [struct.pack("<160h", *[i * 1000 + j - 20000 for j in range(160)]) for i in range(5)]
            producer = client.start(operation="submit.pcm", sequence=0, token=token, body=b"".join(frames))
            received = []
            for frame in frames:
                packet = receiver.recv(4096)
                received.append(time.monotonic_ns())
                check(packet == b"DFPCM01\0" + struct.pack("<Q", 42) + frame,
                      "wrong magic, generation, order or PCM payload")
            code, response = client.finish(producer)
            check(code == 200 and response["accepted_frames"] == 5 and response["next_sequence"] == 5,
                  f"five-frame submission failed: {response}")
            check(response["runtime_id"] == RUNTIME_A and response["generation"] == 42, "wrong response identity")
            intervals = [(b - a) / 1e6 for a, b in zip(received, received[1:])]
            check(all(value >= 18 for value in intervals), f"pacing below 18 ms VM threshold: {intervals}")
            check(int(read_state(state_path)["lease_deadline_ms"]) > original_deadline, "lease did not renew")
            no_datagram(receiver)
            # A separate CGI process with the current token must not replay old audio.
            duplicate = client.call(expected=409, error="sequence_duplicate", operation="submit.pcm",
                                    sequence=0, token=token, body=frames[0])
            check(duplicate["accepted_frames"] == 0 and duplicate["next_sequence"] == 5, "replay advanced state")
            no_datagram(receiver)
            for bad_body, length in [(b"short", 320), (b"".join(frames) + frames[0], 1920)]:
                client.call(expected=400, operation="submit.pcm", sequence=5, token=token,
                            body=bad_body, length=length)
                no_datagram(receiver)
            client.call(operation="session/end", token=token)
            released = read_state(state_path)
            check(released["audio_session"] == "" and released["lease_deadline_ms"] == "0", "lease not released")
            reopened = client.call()
            check(reopened["next_sequence"] == 5 and reopened["audio_session"] != token, "release takeover reset sequence")
            deadline = int(read_state(state_path)["lease_deadline_ms"])
            wait_until(lambda: int(time.clock_gettime(time.CLOCK_MONOTONIC) * 1000) >= deadline, timeout=3)
            takeover = client.call()
            token = takeover["audio_session"]
            check(token != reopened["audio_session"] and takeover["next_sequence"] == 5, "expiry takeover failed")
            producer = client.start(operation="submit.pcm", sequence=5, token=token, body=b"".join(frames))
            for frame in frames[:2]:
                check(receiver.recv(4096) == b"DFPCM01\0" + struct.pack("<Q", 42) + frame, "partial payload mismatch")
            socket_path.unlink()
            code, partial = client.finish(producer)
            check(code == 503 and partial["error"] == "send_unavailable" and
                  partial["accepted_frames"] == 2 and partial["next_sequence"] == 7,
                  f"partial failure lost progress: {partial}")
            no_datagram(receiver)
            check(read_state(state_path)["next_sequence"] == "7", "partial sequence not persisted")

            # Confirm the old request is waiting on the real state flock before
            # changing status. Linux /proc/locks exposes the blocked waiter.
            with state_path.open("r+") as locked:
                fcntl.flock(locked, fcntl.LOCK_EX)
                stale = client.start()
                if sys.platform.startswith("linux"):
                    wait_until(lambda: any("->" in line and f" {stale.pid} " in line
                                          for line in Path("/proc/locks").read_text().splitlines()))
                else:
                    # Host-only fallback: observe the lock fd being opened via a
                    # bounded lsof probe. Actual VM acceptance always uses /proc.
                    wait_until(lambda: subprocess.run(["lsof", "-a", "-p", str(stale.pid), str(state_path)],
                                                       capture_output=True, timeout=2).returncode == 0)
                check(stale.poll() is None, "stale request did not wait for lock")
                secure_write(status_path, status_record(RUNTIME_B, 43))
                fcntl.flock(locked, fcntl.LOCK_UN)
            code, old = client.finish(stale)
            check(code == 409 and old["error"] == "runtime_mismatch" and old["runtime_id"] == RUNTIME_B,
                  f"stale waiter used pre-lock status: {old}")
            current = client.call(runtime=RUNTIME_B, generation=43)
            state_before = state_path.read_bytes()
            client.call(expected=409, error="runtime_mismatch")
            check(state_path.read_bytes() == state_before and read_state(state_path)["runtime_id"] == RUNTIME_B,
                  "old runtime replaced new runtime state")
            check(current["next_sequence"] == 0, "new runtime did not reset sequence")
            no_datagram(receiver)
            print(f"Isolated PCM acceptance passed; pacing intervals (ms): {intervals}", flush=True)
        finally:
            client.close()
            receiver.close()
    check(not root.exists(), "isolated fixture tree was not removed")


def stable_nft(value):
    if isinstance(value, dict):
        return {key: stable_nft(item) for key, item in value.items()
                if key not in {"packets", "bytes", "expires", "used", "age"}}
    if isinstance(value, list):
        return [stable_nft(item) for item in value]
    return value


def platform_snapshot():
    status = json.loads(command(["ubus", "call", "doorfast", "status"]).stdout)
    pids = {}
    for process in Path("/proc").iterdir():
        if process.name.isdigit():
            try:
                if (process / "comm").read_text().strip().startswith("doorfast"):
                    pids[process.name] = (process / "stat").read_text().split(") ", 1)[1].split()[19]
            except FileNotFoundError:
                pass
    services = json.loads(command(["ubus", "call", "service", "list"]).stdout)
    services = {key: value for key, value in services.items() if key.startswith("doorfast")}
    audio = status["audio_tx"]
    return {
        "network": command(["uci", "export", "network"]).stdout,
        "firewall": command(["uci", "export", "firewall"]).stdout,
        "nft": json.dumps(stable_nft(json.loads(command(["nft", "-j", "list", "ruleset"]).stdout)), sort_keys=True),
        "pids": pids, "services": services,
        "audio": {key: audio[key] for key in ("active", "generation", "packets_sent", "packets_failed", "next_sequence")},
        "runtime": status["runtime_id"], "call": status["call"],
    }, status


def installed_state():
    try:
        info = PRODUCTION_STATE.lstat()
    except FileNotFoundError:
        return None
    check(stat.S_ISREG(info.st_mode) and info.st_uid == os.geteuid() and
          stat.S_IMODE(info.st_mode) == 0o600, "unsafe existing production state")
    return PRODUCTION_STATE.read_bytes()


def worker(binary):
    check(os.geteuid() == 0, "VM acceptance must run as root")
    check(binary.name == "doorfast-pcm-http-acceptance" and
          re.fullmatch(r"/tmp/df-pcm-vm\.[A-Za-z0-9]+", str(binary.parent)),
          "worker acceptance executable must be inside its private upload directory")
    before, status = platform_snapshot()
    check(status["call"]["session"] == "idle" and not status["audio_tx"]["active"],
          "VM daemon must already be idle; runner never stops or starts services")
    check(before["pids"], "Doorfast daemon is not running")
    package_list = Path("/lib/apk/packages/doorfast.list").read_text().splitlines()
    check("/usr/sbin/doorfast-pcm-http" in package_list, "installed helper missing from APK list")
    check(not any("doorfast-pcm-http-acceptance" in item for item in package_list), "APK installs acceptance binary")
    original_state = installed_state()
    client = CGI(["/www/cgi-bin/doorfast"])
    runtime = status["runtime_id"]
    generation = max(1, status["call"]["generation"])

    def reject(**kwargs):
        kwargs.setdefault("runtime", runtime)
        kwargs.setdefault("generation", generation)
        client.call(**kwargs)
        after, _ = platform_snapshot()
        check(after == before, "installed rejection changed network, firewall, PID, service or call/audio state")
        # The core may create an empty lock file for a rejected first request;
        # its creation is not a producer transition, and is undone in finally.
        check(installed_state() == original_state or
              (original_state is None and installed_state() == b""), "rejection changed producer state")

    try:
        reject(expected=409)
        reject(expected=400, runtime="invalid")
        # Zero/one have no earlier positive generation; use a mismatched value
        # on a fresh daemon and an actually older generation when available.
        stale_generation = generation - 1 if generation > 1 else generation + 1
        reject(expected=409, generation=stale_generation)
        reject(expected=405, method="GET")
        reject(expected=409, operation="submit.pcm", sequence=0, token="ab" * 16, body=b"short", length=320)
        reject(expected=409, operation="submit.pcm", sequence=0, token="ab" * 16, body=bytes(1920))
        print("Installed idle/malformed/stale/oversized rejection and unchanged daemon counters passed.", flush=True)
        isolated(binary, base=binary.parent)
        binary.unlink()
        check(not binary.exists(), "acceptance executable was not removed")
        reject(expected=409)
    finally:
        client.close()
        binary.unlink(missing_ok=True)
        cleanup_errors = []
        if original_state is None and PRODUCTION_STATE.exists():
            if installed_state() == b"":
                PRODUCTION_STATE.unlink()
            else:
                cleanup_errors.append("production state changed unexpectedly; preserved for inspection")
        if installed_state() != original_state:
            cleanup_errors.append("original producer state not restored")
        after, _ = platform_snapshot()
        if after != before:
            cleanup_errors.append("network/firewall or original Doorfast PID/service/call state changed")
        check(not cleanup_errors, "; ".join(cleanup_errors))
    print("VM platform invariants and production state cleanup passed; no physical playback claim.", flush=True)


def remote_run(ssh, text, data=None, timeout=15):
    return subprocess.run([ssh, text], input=data, capture_output=True, check=True, timeout=timeout)


def remote_bundle(binary):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode="w") as archive:
        for name, contents, mode in (
                ("doorfast-pcm-http-acceptance", binary.read_bytes(), 0o700),
                ("runner.py", Path(__file__).read_bytes(), 0o600)):
            entry = tarfile.TarInfo(name)
            entry.size = len(contents)
            entry.mode = mode
            archive.addfile(entry, io.BytesIO(contents))
    return output.getvalue()


def remote_acceptance(ssh, bundle, timeout=55):
    # One remote shell owns allocation, upload, execution and removal. Its EXIT
    # trap therefore survives local launcher exceptions and SSH disconnects;
    # the worker's alarm bounds cleanup if a disconnected session stays alive.
    command_text = r'''set -eu
root=
cleanup() {
    rc=$?
    trap - EXIT HUP INT TERM
    if [ -n "$root" ]; then
        rm -rf "$root"
        [ ! -e "$root" ] || rc=1
    fi
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
umask 077
root=$(mktemp -d /tmp/df-pcm-vm.XXXXXXXX)
case "$root" in /tmp/df-pcm-vm.*) ;; *) exit 1 ;; esac
tar -x -f - -C "$root"
test -x "$root/doorfast-pcm-http-acceptance"
test -f "$root/runner.py"
python3 "$root/runner.py" --worker "$root/doorfast-pcm-http-acceptance"
'''
    child = subprocess.Popen([ssh, command_text], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        stdout, stderr = child.communicate(bundle, timeout=timeout)
    except BaseException:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
        raise
    if child.returncode:
        raise subprocess.CalledProcessError(child.returncode, child.args, stdout, stderr)
    return stdout, stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ssh", nargs="?", type=Path, help="existing VM SSH wrapper")
    parser.add_argument("acceptance", nargs="?", type=Path, help="Actions target-built acceptance artifact")
    parser.add_argument("--worker", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--isolated", type=Path, help="run fixture layer locally (not VM acceptance)")
    args = parser.parse_args()
    if args.worker:
        def interrupted(signum, _frame):
            raise RuntimeError(f"VM acceptance interrupted by signal {signum}")
        for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP, signal.SIGALRM):
            signal.signal(signum, interrupted)
        signal.alarm(40)
        worker(args.worker.resolve())
        signal.alarm(0)
        return
    if args.isolated:
        isolated(args.isolated.resolve())
        return
    if args.ssh is None or args.acceptance is None:
        parser.error("SSH_WRAPPER and ARTIFACT are required")
    ssh = str(args.ssh.resolve())
    binary = args.acceptance.resolve()
    check(binary.is_file(), "acceptance artifact does not exist")
    # Do not create remote artifacts until prerequisites are known to exist.
    remote_run(ssh, "set -eu; test \"$(id -u)\" = 0; command -v python3; command -v ubus; command -v uci; command -v nft; command -v tar")
    previous_handlers = {}
    def interrupted(signum, _frame):
        raise RuntimeError(f"VM acceptance launcher interrupted by signal {signum}")
    for signum in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        previous_handlers[signum] = signal.signal(signum, interrupted)
    try:
        stdout, _ = remote_acceptance(ssh, remote_bundle(binary))
        print(stdout.decode(), end="")
    except subprocess.CalledProcessError as error:
        sys.stderr.write(error.stdout.decode(errors="replace") + error.stderr.decode(errors="replace"))
        raise
    finally:
        for signum, handler in previous_handlers.items():
            signal.signal(signum, handler)


if __name__ == "__main__":
    main()
