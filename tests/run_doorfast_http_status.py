#!/usr/bin/env python3
"""Read-only smoke test for an installed Doorfast HTTP bridge."""
import json
import sys
import urllib.request

if len(sys.argv) != 2:
    raise SystemExit("usage: run_doorfast_http_status.py http://host/cgi-bin/doorfast")
base = sys.argv[1].rstrip("/")
with urllib.request.urlopen(base + "/api/v1/status", timeout=10) as response:
    payload = json.load(response)
if not isinstance(payload, dict) or payload.get("running") is not True:
    raise SystemExit(f"unexpected Doorfast status: {payload!r}")
print(json.dumps(payload, ensure_ascii=False, sort_keys=True))
