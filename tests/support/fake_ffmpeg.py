#!/usr/bin/env python3
"""A deterministic, payload-blind FFmpeg stand-in for acceptance tests."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import sys
from urllib.parse import urlsplit


def _argument_url(argv):
    for value in reversed(argv):
        if value.startswith("rtsp://"):
            return value
    raise SystemExit("fake_ffmpeg requires an RTSP output URL")


def _request(stream: socket.socket, method: str, uri: str, cseq: int, body: bytes = b"") -> None:
    headers = [f"{method} {uri} RTSP/1.0", f"CSeq: {cseq}"]
    if body:
        headers.extend(["Content-Type: application/sdp", f"Content-Length: {len(body)}"])
    stream.sendall(("\r\n".join(headers) + "\r\n\r\n").encode("ascii") + body)
    stream.recv(4096)


def main(argv=None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(add_help=False)
    parser.parse_known_args(argv)
    if os.environ.get("FAKE_FFMPEG_NO_X264") == "1":
        return 127
    if os.environ.get("FAKE_FFMPEG_EXIT") == "1" or os.environ.get("FAKE_FFMPEG_CHILD_EXIT") == "1":
        return 1
    output = _argument_url(argv)
    parsed = urlsplit(output)
    uri = output.split("@", 1)[-1]
    if parsed.hostname is None or parsed.port is None:
        return 2
    stop = False
    payload_hash = hashlib.sha256()
    payload_bytes = 0

    def handle_signal(_signum, _frame):
        nonlocal stop
        stop = True

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)
    sock = socket.create_connection((parsed.hostname, parsed.port), timeout=3)
    sock.settimeout(3)
    try:
        # The body describes an H.264 track but contains no test payload.
        sdp = ("v=0\r\n" "o=- 0 0 IN IP4 127.0.0.1\r\n"
               "s=doorfast\r\n" "t=0 0\r\n" "m=video 0 RTP/AVP 96\r\n"
               "a=rtpmap:96 H264/90000\r\n").encode("ascii")
        _request(sock, "ANNOUNCE", uri, 1, sdp)
        _request(sock, "RECORD", uri, 2)
        evidence_dir = os.environ.get("DF_FAKE_FFMPEG_EVIDENCE_DIR")
        if evidence_dir:
            stream = parsed.path.strip("/").replace("/", "_")
            (Path(evidence_dir) / f"{stream}.ready").touch()
        while not stop:
            data = os.read(sys.stdin.fileno(), 4096)
            if not data:
                break
            payload_hash.update(data)
            payload_bytes += len(data)
        _request(sock, "TEARDOWN", uri, 3)
    except (ConnectionError, OSError, TimeoutError):
        return 3
    finally:
        sock.close()
    evidence_dir = os.environ.get("DF_FAKE_FFMPEG_EVIDENCE_DIR")
    if evidence_dir:
        stream = parsed.path.strip("/").replace("/", "_")
        evidence = Path(evidence_dir) / f"{stream}.json"
        evidence.write_text(json.dumps({
            "bytes": payload_bytes,
            "sha256": payload_hash.hexdigest(),
        }, sort_keys=True) + "\n")
        (Path(evidence_dir) / f"{stream}.ready").unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
