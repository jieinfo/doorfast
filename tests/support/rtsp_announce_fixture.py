#!/usr/bin/env python3
"""Small RTSP ANNOUNCE/RECORD sink for the local media acceptance run.

The fixture deliberately stores protocol metadata only.  It never records SDP,
credentials, or RTP/JPEG bytes.
"""

from __future__ import annotations

import argparse
import json
import socket
import threading
from pathlib import Path
from urllib.parse import urlsplit


class RtspAnnounceFixture:
    def __init__(self, host: str = "127.0.0.1") -> None:
        self.host = host
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((host, 0))
        self.server.listen(4)
        self.server.settimeout(0.2)
        self.port = self.server.getsockname()[1]
        self.announced_path = None
        self.transactions = []
        self.producer_count = 0
        self.max_producer_count = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, name="rtsp-fixture", daemon=True)
        self._thread.start()

    @property
    def address(self) -> str:
        return f"{self.host}:{self.port}"

    def url(self, path: str = "/doorfast_preview") -> str:
        return f"rtsp://{self.address}{path}"

    def _serve(self) -> None:
        while not self._stop.is_set():
            try:
                client, _ = self.server.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            client.settimeout(2)
            threading.Thread(target=self._client, args=(client,), daemon=True).start()

    @staticmethod
    def _read_request(stream: socket.socket):
        data = bytearray()
        while b"\r\n\r\n" not in data and len(data) <= 65536:
            chunk = stream.recv(4096)
            if not chunk:
                return None
            data.extend(chunk)
        if b"\r\n\r\n" not in data:
            return None
        header_bytes, body = bytes(data).split(b"\r\n\r\n", 1)
        lines = header_bytes.decode("ascii", "replace").split("\r\n")
        request = lines[0].split(" ")
        if len(request) != 3:
            return None
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                key, value = line.split(":", 1)
                headers[key.lower()] = value.strip()
        length = int(headers.get("content-length", "0") or "0")
        while len(body) < length:
            chunk = stream.recv(min(4096, length - len(body)))
            if not chunk:
                return None
            body += chunk
        return request[0], request[1], headers.get("cseq", ""), headers

    def _client(self, client: socket.socket) -> None:
        active = False
        try:
            while True:
                request = self._read_request(client)
                if request is None:
                    return
                method, uri, cseq, _headers = request
                path = urlsplit(uri).path or "/"
                if method in {"ANNOUNCE", "RECORD", "TEARDOWN", "OPTIONS", "SETUP"}:
                    self.transactions.append({"method": method, "path": path})
                if method == "ANNOUNCE":
                    self.announced_path = path
                elif method == "RECORD" and not active:
                    active = True
                    self.producer_count += 1
                    self.max_producer_count = max(self.max_producer_count, self.producer_count)
                elif method == "TEARDOWN" and active:
                    active = False
                    self.producer_count = max(0, self.producer_count - 1)
                response = f"RTSP/1.0 200 OK\r\nCSeq: {cseq}\r\n\r\n".encode("ascii")
                client.sendall(response)
        except (ConnectionError, OSError, ValueError):
            return
        finally:
            if active:
                self.producer_count = max(0, self.producer_count - 1)
            client.close()

    def snapshot(self) -> dict:
        return {
            "announced_path": self.announced_path,
            "transactions": list(self.transactions),
            "producer_count": self.producer_count,
            "max_producer_count": self.max_producer_count,
        }

    def write_snapshot(self, path: Path) -> None:
        path.write_text(json.dumps(self.snapshot(), indent=2, sort_keys=True) + "\n")

    def close(self) -> None:
        self._stop.set()
        try:
            self.server.close()
        finally:
            self._thread.join(timeout=2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    fixture = RtspAnnounceFixture()
    print(json.dumps({"host": fixture.host, "port": fixture.port}), flush=True)
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        if args.output:
            fixture.write_snapshot(args.output)
        fixture.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
