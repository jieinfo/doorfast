#!/bin/sh
set -eu

make pcm-submit >/dev/null
workspace=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-pcm-submit.XXXXXXXX")
receiver_pid=''
trap 'if [ -n "$receiver_pid" ]; then kill "$receiver_pid" 2>/dev/null || true; fi; rm -rf "$workspace"' EXIT
socket="$workspace/audio.sock"
packet="$workspace/packet"

python3 - "$socket" "$packet" <<'PY' &
import os
import socket
import sys

path, output = sys.argv[1:]
server = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
server.bind(path)
os.chmod(path, 0o600)
data = server.recv(337)
with open(output, "wb") as stream:
    stream.write(data)
PY
receiver_pid=$!

attempt=0
while [ ! -S "$socket" ]; do
  attempt=$((attempt + 1))
  [ "$attempt" -lt 100 ]
  sleep 0.01
done

python3 - <<'PY' | build/doorfast-pcm-submit 23 "$socket"
import sys
sys.stdout.buffer.write(bytes(range(256)) + bytes(range(64)))
PY
wait "$receiver_pid"
receiver_pid=''

python3 - "$packet" <<'PY'
import sys

packet = open(sys.argv[1], "rb").read()
assert len(packet) == 336
assert packet[:8] == b"DFPCM01\0"
assert int.from_bytes(packet[8:16], "little") == 23
assert packet[16:] == bytes(range(256)) + bytes(range(64))
PY

python3 - "$workspace/full.sock" <<'PY'
import os
import socket
import subprocess
import sys

path = sys.argv[1]
server = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
server.bind(path)
os.chmod(path, 0o600)
for attempt in range(256):
    try:
        result = subprocess.run(
            ["build/doorfast-pcm-submit", "23", path],
            input=bytes(320),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=1,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError("PCM submit blocked on a full receive queue") from error
    if result.returncode == 3:
        break
    assert result.returncode == 0, result.returncode
else:
    raise AssertionError("PCM receive queue did not reach its bounded capacity")
PY

if python3 - 2>/dev/null <<'PY' | build/doorfast-pcm-submit 0 "$socket" >/dev/null 2>&1
import sys
sys.stdout.buffer.write(bytes(320))
PY
then
  exit 1
fi
if python3 - 2>/dev/null <<'PY' | build/doorfast-pcm-submit 23 "$socket" >/dev/null 2>&1
import sys
sys.stdout.buffer.write(bytes(319))
PY
then
  exit 1
fi
if python3 - 2>/dev/null <<'PY' | build/doorfast-pcm-submit 23 "$socket" >/dev/null 2>&1
import sys
sys.stdout.buffer.write(bytes(321))
PY
then
  exit 1
fi
