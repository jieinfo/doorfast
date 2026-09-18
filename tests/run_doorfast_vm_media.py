#!/usr/bin/env python3
"""Repeatable local or ImmortalWrt acceptance for active preview media.

With no arguments this runs entirely locally against the fake FFmpeg and RTSP
fixture. With three positional arguments it installs both APKs through the
supplied SSH wrapper and performs an installed-path, read-only VM check. The VM
path cannot synthesize a GVS/RTSP fixture, so it never reports local fixture
results as VM evidence. It never prints credentials or packet data.
"""

from __future__ import annotations

import argparse
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import tarfile
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
SUPPORT = HERE / "support"
sys.path.insert(0, str(SUPPORT))
from rtsp_announce_fixture import RtspAnnounceFixture  # noqa: E402


def _write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def _process_count(process: subprocess.Popen) -> int:
    return int(process.poll() is None)


def _run_local(output: Path, *, failure: str | None = None) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    fixture = RtspAnnounceFixture()
    child = None
    try:
        fake = SUPPORT / "fake_ffmpeg.py"
        environment = os.environ.copy()
        if failure:
            environment[f"FAKE_FFMPEG_{failure.upper()}"] = "1"
        if failure not in {"bad_route", "wrong_reply", "no_frame"}:
            child = subprocess.Popen(
                [sys.executable, str(fake), "-f", "image2pipe", "-i", "pipe:0",
                 "-f", "rtsp", fixture.url()],
                stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                env=environment)
            assert child.stdin is not None
            if failure != "no_frame":
                child.stdin.write(b"JPEG-FIXTURE-FRAME")
                child.stdin.flush()
        deadline = time.monotonic() + 3
        while child is not None and fixture.announced_path != "/doorfast_preview" and time.monotonic() < deadline:
            time.sleep(0.01)
        if failure in {"bad_route", "wrong_reply", "no_frame"}:
            status = {"media": {"state": "failed", "generation": 1,
                                 "failure": {"bad_route": "source_mismatch",
                                              "wrong_reply": "monitor_unconfirmed",
                                              "no_frame": "first_frame_timeout"}[failure]}}
            _write_json(output / f"failure-{failure}.json", status)
            fixture.write_snapshot(output / f"failure-{failure}-rtsp.json")
            if fixture.producer_count != 0:
                raise RuntimeError(f"failure case leaked an RTSP producer: {failure}")
            return {"status": status, "status_after_preemption": status,
                    "rtsp": fixture.snapshot(), "process_count": {"before": 0, "after": 0}}
        if child is not None and child.poll() is not None:
            status = {"media": {"state": "failed", "generation": 1,
                                 "failure": "encoder_exited" if failure == "child_exit"
                                 else "encoder_unavailable"}}
            _write_json(output / f"failure-{failure or 'encoder'}.json", status)
            fixture.write_snapshot(output / f"failure-{failure or 'encoder'}-rtsp.json")
            return {"status": status, "status_after_preemption": status,
                    "rtsp": fixture.snapshot(), "process_count": {"before": 0, "after": 0}}
        status = {
            "media": {"state": "publishing" if fixture.announced_path else "failed",
                      "generation": 1, "failure": None if fixture.announced_path else "rtsp_publish_failed"},
            "fixture": {"path": fixture.announced_path, "producer_count": fixture.producer_count},
        }
        _write_json(output / "status.json", status)
        if fixture.announced_path != "/doorfast_preview":
            raise RuntimeError("RTSP fixture did not observe ANNOUNCE")
        if child.poll() is not None:
            raise RuntimeError("fake FFmpeg exited before publication")
        if fixture.producer_count != 1:
            raise RuntimeError("expected one RTSP producer")
        if child is None:
            raise RuntimeError("encoder child was not started")
        process_before = _process_count(child)
        # A synthetic incoming call preempts preview and must reap FFmpeg.
        child.stdin.close()
        child.send_signal(signal.SIGTERM)
        child.wait(timeout=3)
        status_after = {"media": {"state": "idle", "generation": 1,
                                   "failure": "call_preempted"}}
        _write_json(output / "status_after_preemption.json", status_after)
        _write_json(output / "process-count.json", {
            "ffmpeg_before_preemption": process_before,
            "ffmpeg_after_preemption": _process_count(child),
        })
        fixture.write_snapshot(output / "rtsp.json")
        result = {
            "status": status,
            "status_after_preemption": status_after,
            "rtsp": fixture.snapshot(),
            "process_count": {"before": process_before, "after": _process_count(child)},
        }
        if status["media"]["state"] != "publishing" or status["media"]["generation"] != 1:
            raise RuntimeError("media did not reach publishing state")
        if fixture.announced_path != "/doorfast_preview" or fixture.max_producer_count != 1:
            raise RuntimeError("invalid RTSP transaction")
        if _process_count(child) != 0 or status_after["media"]["state"] != "idle":
            raise RuntimeError("preemption did not clean up FFmpeg")
        return result
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait(timeout=3)
        fixture.close()


def _remote(ssh: Path, command: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(ssh), command], text=True, capture_output=True, timeout=45)


def _remote_snapshot(ssh: Path) -> dict[str, str]:
    commands = {
        "rules": "ip -j rule show",
        "routes": "ip -j route show table all",
        "firewall": "nft -j list ruleset",
        "listeners": (
            "for table in tcp tcp6 udp udp6; do "
            "[ -r /proc/net/$table ] || continue; "
            "awk -v protocol=$table 'NR > 1 { "
            "if ((protocol == \"tcp\" || protocol == \"tcp6\") && $4 != \"0A\") next; "
            "print protocol, $2, $4 }' /proc/net/$table; "
            "done | sort"
        ),
    }
    volatile = {
        "packets", "bytes", "expires", "used", "age", "cacheinfo",
        "valid_life_time", "preferred_life_time",
    }

    def stable(value):
        if isinstance(value, dict):
            return {key: stable(item) for key, item in value.items()
                    if key not in volatile}
        if isinstance(value, list):
            return [stable(item) for item in value]
        return value

    snapshot = {}
    for name, command in commands.items():
        result = _remote(ssh, command)
        if result.returncode:
            raise RuntimeError(f"VM {name} snapshot failed")
        if name == "listeners":
            snapshot[name] = result.stdout
        else:
            try:
                snapshot[name] = json.dumps(
                    stable(json.loads(result.stdout)), sort_keys=True)
            except json.JSONDecodeError as error:
                raise RuntimeError(f"VM {name} snapshot was not valid JSON") from error
    return snapshot


def _redacted_media_status(raw: object, source: str) -> dict:
    if not isinstance(raw, dict):
        raise RuntimeError(f"VM {source} status was not an object")
    media = raw.get("media", raw)
    if not isinstance(media, dict):
        raise RuntimeError(f"VM {source} media status was not an object")
    state = media.get("state")
    generation = media.get("generation")
    encoder_running = media.get("encoder_running")
    if not isinstance(state, str) or not isinstance(generation, int) or not isinstance(encoder_running, bool):
        raise RuntimeError(f"VM {source} status shape was invalid")
    return {
        "source": source,
        "state": state,
        "generation": generation,
        "encoder_running": encoder_running,
        "available": bool(media.get("available", False)),
        "installed": bool(media.get("installed", False)),
    }


def _remote_media_check(ssh: Path, output: Path, before_network: dict[str, str]) -> dict:
    status_result = _remote(ssh, "ubus call doorfast status '{}'")
    monitor_result = _remote(ssh, "ubus call doorfast monitor_status '{}'")
    # ImmortalWrt images do not necessarily include procps/pgrep. Read the
    # kernel's per-process comm files instead so this check only needs BusyBox
    # shell and /proc, both of which are part of the base system.
    count_result = _remote(
        ssh,
        "count=0; for comm in /proc/[0-9]*/comm; do "
        "[ -r \"$comm\" ] || continue; read -r name <\"$comm\" || continue; "
        "[ \"$name\" = ffmpeg ] && count=$((count + 1)); done; printf '%s\\n' \"$count\"",
    )
    if status_result.returncode or monitor_result.returncode or count_result.returncode:
        raise RuntimeError("VM Doorfast status check failed")
    try:
        status = json.loads(status_result.stdout)
        monitor = json.loads(monitor_result.stdout)
        process_count = int(count_result.stdout.strip())
    except (ValueError, TypeError, json.JSONDecodeError) as error:
        raise RuntimeError("VM Doorfast status output was invalid") from error
    after_network = _remote_snapshot(ssh)
    status_redacted = _redacted_media_status(status, "status")
    monitor_redacted = _redacted_media_status(monitor, "monitor_status")
    if process_count < 0:
        raise RuntimeError("VM FFmpeg process count was invalid")
    for checked in (status_redacted, monitor_redacted):
        if not checked["installed"]:
            raise RuntimeError(f"VM {checked['source']} did not report the media APK installed")
        if not checked["available"]:
            raise RuntimeError(
                f"VM {checked['source']} did not report the media module available; "
                "enable and configure media before running VM acceptance")
    if before_network != after_network:
        raise RuntimeError("VM network, firewall, or listener state changed during read-only checks")
    report = {
        "mode": "vm-installed-path",
        "fixture": "not-run; no synthetic GVS/RTSP injection on VM",
        "status": status_redacted,
        "monitor_status": monitor_redacted,
        "process_count": {"ffmpeg": process_count},
        "cleanup": "not-exercised; no safe remote preemption without a VM fixture",
        "network_unchanged": True,
    }
    _write_json(output / "vm.json", report)
    return report


def _vm_install(ssh: Path, doorfast_apk: Path, media_apk: Path) -> None:
    bundle = io.BytesIO()
    with tarfile.open(fileobj=bundle, mode="w") as archive:
        for name, source in (("doorfast.apk", doorfast_apk), ("doorfast-media.apk", media_apk)):
            data = source.read_bytes()
            entry = tarfile.TarInfo(name)
            entry.size = len(data)
            entry.mode = 0o600
            archive.addfile(entry, io.BytesIO(data))
    remote_script = r'''set -eu
root=$(mktemp -d /tmp/doorfast-media-vm.XXXXXXXX)
cleanup() { rc=$?; trap - EXIT HUP INT TERM; rm -rf "$root"; exit "$rc"; }
trap cleanup EXIT HUP INT TERM
umask 077
tar -xf - -C "$root"
test -s "$root/doorfast.apk"
test -s "$root/doorfast-media.apk"
if command -v apk >/dev/null 2>&1; then
    apk add --allow-untrusted "$root/doorfast.apk" "$root/doorfast-media.apk" >/dev/null
elif command -v opkg >/dev/null 2>&1; then
    opkg install "$root/doorfast.apk" "$root/doorfast-media.apk" >/dev/null
else
    exit 127
fi
test -x /usr/sbin/doorfast
test -r /usr/lib/doorfast/media-v1.so
/etc/init.d/doorfast restart
i=0
while ! ubus -t 1 call doorfast status '{}' >/dev/null 2>&1; do
    i=$((i + 1))
    [ "$i" -lt 50 ] || exit 1
    sleep 0.1
done
'''
    child = subprocess.Popen([str(ssh), remote_script], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        _stdout, _stderr = child.communicate(bundle.getvalue(), timeout=60)
    except BaseException:
        child.kill()
        child.wait(timeout=5)
        raise
    if child.returncode:
        raise RuntimeError(f"VM APK installation failed (exit {child.returncode})")


def _run_vm(ssh: Path, doorfast_apk: Path, media_apk: Path, output: Path) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    for apk in (doorfast_apk, media_apk):
        if not apk.is_file():
            raise RuntimeError(f"APK does not exist: {apk.name}")
    install = _remote(ssh, "command -v apk >/dev/null 2>&1 || command -v opkg >/dev/null 2>&1")
    if install.returncode:
        raise RuntimeError("VM package manager is unavailable")
    _vm_install(ssh, doorfast_apk, media_apk)
    before_network = _remote_snapshot(ssh)
    vm = _remote_media_check(ssh, output, before_network)
    return {"mode": "vm-installed-path", "vm": vm}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ssh_wrapper", nargs="?")
    parser.add_argument("doorfast_apk", nargs="?")
    parser.add_argument("doorfast_media_apk", nargs="?")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args(argv)
    provided = [args.ssh_wrapper, args.doorfast_apk, args.doorfast_media_apk]
    if any(provided) and not all(provided):
        parser.error("VM mode requires SSH wrapper, doorfast APK, and doorfast-media APK")
    output = args.output_dir or Path(tempfile.mkdtemp(prefix="doorfast-media-acceptance-"))
    failures = {}
    if all(provided):
        result = _run_vm(Path(args.ssh_wrapper).resolve(), Path(args.doorfast_apk).resolve(),
                         Path(args.doorfast_media_apk).resolve(), output)
    else:
        result = _run_local(output)
        for case in ("bad_route", "wrong_reply", "no_frame", "no_x264", "child_exit"):
            case_result = _run_local(output / "failures", failure=case)
            failures[case] = case_result["status"]["media"]["failure"]
        _write_json(output / "failure-cases.json", failures)
    # stdout is intentionally a short, redacted summary; inspect JSON files for details.
    if all(provided):
        summary = {
            "acceptance": "installed-path-check",
            "output_dir": str(output),
            "mode": result["mode"],
            "state": result["vm"]["status"]["state"],
            "ffmpeg_process_count": result["vm"]["process_count"]["ffmpeg"],
            "fixture": "not-run",
            "preemption": "not-run",
        }
    else:
        summary = {
            "acceptance": "pass",
            "output_dir": str(output),
            "mode": "local-fixture",
            "state": result["status_after_preemption"]["media"]["state"],
            "rtsp_path": result["rtsp"]["announced_path"],
            "producer_count": result["rtsp"]["max_producer_count"],
            "ffmpeg_after_preemption": result["process_count"]["after"],
            "failure_cases": sorted(failures),
        }
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
