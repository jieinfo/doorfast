#!/usr/bin/env python3
"""Verify Doorfast station discovery and configured-station snapshots.

The wrapper performs one active discovery request, reads the candidate cache
and both configured-station projections, and records software-derived
multicast values. Candidate replies are observations only; this runner never
adopts or writes a station. The SSH fixture may expose ``frames_sent`` in the
scan response so the acceptance can verify the complete three-frame burst.
"""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
from pathlib import Path
import subprocess
import tempfile

RUNTIME_ID_CHARS = set("0123456789abcdef")
CANDIDATE_FIELDS = (
    "logical_address", "ipv4", "first_seen_ms", "last_seen_ms",
    "reply_count", "configured",
)


def valid_runtime_id(value: object) -> bool:
    return (isinstance(value, str) and len(value) == 16 and
            value == value.lower() and set(value) <= RUNTIME_ID_CHARS)


def redacted_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()[:16]


def redact_candidate(value: dict) -> dict:
    return {
        "logical_address_hash": redacted_hash(value["logical_address"]),
        "ipv4_hash": redacted_hash(value["ipv4"]),
        "first_seen_ms": value["first_seen_ms"],
        "last_seen_ms": value["last_seen_ms"],
        "reply_count": value["reply_count"],
        "configured": value["configured"],
    }


def redact_station(value: dict) -> dict:
    return {
        "id": value["id"],
        "logical_address_hash": redacted_hash(value["logical_address"]),
        "route_source": value["route_source"],
        "route_fresh": value["route_fresh"],
        "monitorable": value["monitorable"],
        "last_seen_ms": value.get("last_seen_ms"),
    }


def remote(ssh: Path, command: str) -> str:
    try:
        result = subprocess.run(
            [str(ssh), command], text=True, capture_output=True, timeout=30
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"VM command timed out: {command}") from error
    if result.returncode:
        raise RuntimeError(
            f"VM command failed ({result.returncode}): {command}: "
            f"{result.stderr.strip()}"
        )
    return result.stdout


def remote_json(ssh: Path, command: str) -> dict:
    try:
        value = json.loads(remote(ssh, command))
    except json.JSONDecodeError as error:
        raise RuntimeError(f"VM command returned invalid JSON: {command}") from error
    if not isinstance(value, dict):
        raise RuntimeError(f"VM command returned a non-object: {command}")
    return value


def parse_identity(value: str) -> tuple[int, int, int, int]:
    prefix, fields = value.strip().split(":", 1)
    if prefix != "IS":
        raise ValueError("GVS identity is not prefixed with IS")
    building, unit, room, machine = (int(field) for field in fields.split("-"))
    floor, room_number = divmod(room, 100)
    if not (1 <= building <= 99 and 1 <= unit <= 9 and
            1 <= floor <= 63 and 1 <= room_number <= 32 and
            1 <= machine <= 4):
        raise ValueError("GVS identity fields are outside the supported range")
    return building * 10 + unit, floor, room_number, machine


def derived_multicast(identity: str) -> str:
    building_unit, floor, room, _machine = parse_identity(identity)
    value = (building_unit - 1) * 2528 + 1024 + (floor - 1) * 32 + room
    return f"238.{(value >> 16) & 0xff}.{(value >> 8) & 0xff}.{value & 0xff}"


def multicast_snapshot(ssh: Path) -> dict[str, str]:
    identity = remote(ssh, "uci -q get doorfast.main.gvs_local_address").strip()
    mode = remote(ssh, "uci -q get doorfast.main.multicast_mode").strip() or "auto"
    configured = remote(ssh, "uci -q get doorfast.main.multicast_address").strip()
    derived = derived_multicast(identity)
    if mode not in {"auto", "custom"}:
        raise RuntimeError(f"unsupported multicast mode: {mode!r}")
    if mode == "custom":
        try:
            effective = str(ipaddress.IPv4Address(configured))
        except ipaddress.AddressValueError as error:
            raise RuntimeError("custom multicast address is invalid") from error
        if not ipaddress.IPv4Address(effective) in ipaddress.IPv4Network(
                "224.0.0.0/4"):
            raise RuntimeError("custom multicast address is not in 224.0.0.0/4")
    else:
        effective = derived
    return {
        "identity": identity,
        "mode": mode,
        "derived_group": derived,
        "effective_group": effective,
        "port": "8300",
    }


def run(ssh: Path, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    before = remote(ssh, "uci export network")
    status = remote_json(ssh, "ubus call doorfast status '{}'")
    runtime_id = status.get("runtime_id")
    if not valid_runtime_id(runtime_id):
        raise RuntimeError("status did not expose a valid runtime_id")

    response = remote_json(ssh, "ubus call doorfast station_scan '{}'")
    if response.get("runtime_id") != runtime_id or response.get("scheduled") is not True:
        raise RuntimeError(f"station scan response mismatch: {response!r}")
    scan_sent = response.get("frames_sent", response.get("frames_scheduled"))
    if not isinstance(scan_sent, int) or scan_sent != 3:
        raise RuntimeError("station scan did not report all three emitted frames")
    candidates = remote_json(ssh, "ubus call doorfast station_candidates '{}'")
    stations = remote_json(ssh, "ubus call doorfast stations '{}'")
    http_stations = remote_json(
        ssh, "wget -qO- http://127.0.0.1/cgi-bin/doorfast/api/v1/stations")
    if candidates.get("runtime_id") != runtime_id or stations.get("runtime_id") != runtime_id:
        raise RuntimeError("station response runtime_id changed during acceptance")
    candidate_rows = candidates.get("candidates")
    station_rows = stations.get("stations")
    if not isinstance(candidate_rows, list) or len(candidate_rows) != 3:
        raise RuntimeError("expected exactly three discovery candidates")
    if not isinstance(station_rows, list) or len(station_rows) != 2:
        raise RuntimeError("expected exactly two configured stations")
    configured_candidates = []
    unconfigured_candidates = []
    redacted_candidates = []
    for candidate in candidate_rows:
        if not isinstance(candidate, dict):
            raise RuntimeError("candidate row is not an object")
        if set(candidate) != set(CANDIDATE_FIELDS):
            raise RuntimeError("candidate contains unexpected or missing fields")
        for field in CANDIDATE_FIELDS:
            if field not in candidate:
                raise RuntimeError(f"candidate is missing {field}")
        redacted_candidates.append(redact_candidate(candidate))
        (configured_candidates if candidate["configured"] else unconfigured_candidates).append(candidate)
    if len(configured_candidates) != 2 or len(unconfigured_candidates) != 1:
        raise RuntimeError("candidate/configured separation is invalid")
    redacted_stations = []
    for station in station_rows:
        if not isinstance(station, dict):
            raise RuntimeError("configured station row is not an object")
        for field in ("id", "logical_address", "route_source", "route_fresh",
                      "monitorable"):
            if field not in station:
                raise RuntimeError(f"configured station is missing {field}")
        if not isinstance(station["route_fresh"], bool):
            raise RuntimeError("route_fresh is not boolean")
        redacted_stations.append(redact_station(station))
    configured_addresses = {candidate["logical_address"] for candidate in configured_candidates}
    station_addresses = {station["logical_address"] for station in station_rows}
    if station_addresses != configured_addresses:
        raise RuntimeError("configured station rows do not match configured candidates")
    if unconfigured_candidates[0]["logical_address"] in station_addresses:
        raise RuntimeError("unconfigured candidate leaked into configured stations")
    if http_stations.get("runtime_id") != runtime_id:
        raise RuntimeError("HTTP station list runtime_id mismatch")
    http_rows = http_stations.get("stations")
    if not isinstance(http_rows, list) or len(http_rows) != 2:
        raise RuntimeError("HTTP station list does not contain exactly two stations")
    http_addresses = {row.get("logical_address") for row in http_rows if isinstance(row, dict)}
    if http_addresses != station_addresses:
        raise RuntimeError("HTTP station list does not match configured stations")

    revision = stations.get("revision")
    if not isinstance(revision, int) or revision < 1:
        raise RuntimeError("configured station revision is invalid")
    after = remote(ssh, "uci export network")
    if before != after:
        raise RuntimeError("station discovery changed network UCI state")

    report = {
        "mode": "vm-software-acceptance",
        "network_unchanged": True,
        "multicast": multicast_snapshot(ssh),
        "scan_sent": scan_sent,
        "candidates": redacted_candidates,
        "configured_stations": redacted_stations,
        "configured_revision": revision,
        "physical_registration": "unconfirmed",
        "physical_actions": "unconfirmed",
    }
    (output / "stations.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    )
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ssh_wrapper", type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args(argv)
    output = args.output_dir or Path(tempfile.mkdtemp(prefix="doorfast-stations-"))
    try:
        report = run(args.ssh_wrapper.resolve(), output)
    except (OSError, RuntimeError, ValueError) as error:
        parser.exit(1, f"station acceptance failed: {error}\n")
    print(json.dumps({
        "acceptance": report["mode"],
        "output_dir": str(output),
        "scan_sent": report["scan_sent"],
        "candidate_count": len(report["candidates"]),
        "configured_station_count": len(report["configured_stations"]),
        "configured_revision": report["configured_revision"],
        "physical_registration": report["physical_registration"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
