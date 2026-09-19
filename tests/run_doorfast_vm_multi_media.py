#!/usr/bin/env python3
"""Run host ABI v3 acceptance after checking an installed VM module."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
SUPPORT = HERE / "support"
PROJECT = HERE.parent
MEDIA_SOURCES = (
    "gvs_video_reassembly.c",
    "gvs_monitor.c",
    "gvs_station.c",
    "media_capacity.c",
    "media_credentials.c",
    "media_encoder.c",
    "media_frame_queue.c",
    "media_module_config.c",
    "media_session.c",
    "media_session_manager.c",
)
EXPECTED_ENCODER_EVIDENCE = {
    "doorfast_gate_call": {
        "bytes": 5,
        "sha256": "dec252baa05e53819b757db5116ec4add03894adafa0aae2f4182beaf257fb41",
    },
    "doorfast_gate_main": {
        "bytes": 5,
        "sha256": "355809dc46166fd61e20ddbfb70b7afa0d08f73cdf0e88abef99999cc5138bb1",
    },
    "doorfast_gate_side": {
        "bytes": 5,
        "sha256": "e7df0374e6841e87c72088e3b7ea1cc3277ecef228721714996442ac85241ace",
    },
}


def _remote(ssh_wrapper: Path, command: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(ssh_wrapper), command],
        text=True,
        capture_output=True,
        timeout=45,
    )


def _validate_vm(ssh_wrapper: Path) -> dict:
    service = _remote(ssh_wrapper, "/etc/init.d/doorfast status")
    status_result = _remote(ssh_wrapper, "ubus call doorfast status '{}'")
    if service.returncode or status_result.returncode:
        raise RuntimeError("VM Doorfast service is unavailable")
    try:
        status = json.loads(status_result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError("VM Doorfast status was not valid JSON") from error
    media = status.get("media")
    if not status.get("running") or not isinstance(media, dict):
        raise RuntimeError("VM Doorfast status shape was invalid")
    if not media.get("installed") or not media.get("available"):
        raise RuntimeError("VM media module is unavailable")
    return {
        "running": True,
        "runtime_id": status.get("runtime_id"),
        "media_installed": True,
        "media_available": True,
    }


def _available_memory_kib() -> int:
    meminfo = Path("/proc/meminfo")
    if meminfo.is_file():
        for line in meminfo.read_text().splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1])
    vm_stat = subprocess.run(["vm_stat"], text=True, capture_output=True, timeout=3)
    if vm_stat.returncode:
        raise RuntimeError("available memory could not be measured")
    page_size = 4096
    pages = 0
    for line in vm_stat.stdout.splitlines():
        if "page size of" in line:
            page_size = int(line.split("page size of", 1)[1].split()[0])
        elif line.startswith(("Pages free:", "Pages inactive:", "Pages speculative:")):
            pages += int(line.split(":", 1)[1].strip().rstrip("."))
    if not pages:
        raise RuntimeError("available memory could not be measured")
    return pages * page_size // 1024


def _write_ffmpeg_wrapper(directory: Path) -> None:
    wrapper = directory / "ffmpeg"
    wrapper.write_text(
        "#!/bin/sh\nexec python3 " + repr(str(SUPPORT / "fake_ffmpeg.py")) + " \"$@\"\n"
    )
    wrapper.chmod(0o700)


def _build_harness(source_root: Path, output: Path) -> Path:
    source_dir = source_root / "src"
    sources = [source_dir / name for name in MEDIA_SOURCES]
    missing = [str(path) for path in sources if not path.is_file()]
    if missing:
        raise RuntimeError("media ABI v3 source set is incomplete: " + ", ".join(missing))
    binary = output / "media-v3-acceptance"
    command = [
        os.environ.get("CC", "cc"), "-D_DEFAULT_SOURCE", "-std=c17",
        "-Wall", "-Wextra", "-Werror", "-pedantic", f"-I{source_dir}",
        str(SUPPORT / "media_v3_acceptance.c"),
        *(str(path) for path in sources), "-o", str(binary),
    ]
    compiled = subprocess.run(command, text=True, capture_output=True, timeout=45)
    if compiled.returncode:
        raise RuntimeError("media ABI v3 harness build failed: " + compiled.stderr.strip())
    return binary


def _read_encoder_evidence(directory: Path) -> dict:
    evidence = {}
    for path in sorted(directory.glob("*.json")):
        evidence[path.stem] = json.loads(path.read_text())
    return evidence


def _run_harness(binary: Path, output: Path, available_kib: int) -> tuple[dict, dict, dict]:
    from support.rtsp_announce_fixture import RtspAnnounceFixture

    rtsp = RtspAnnounceFixture()
    fake_bin = output / "fake-bin"
    fake_bin.mkdir(exist_ok=True)
    evidence_dir = Path(tempfile.mkdtemp(prefix="encoder-pipe-", dir=output))
    _write_ffmpeg_wrapper(fake_bin)
    environment = os.environ.copy()
    environment["PATH"] = str(fake_bin) + os.pathsep + environment.get("PATH", "")
    environment["DF_FAKE_FFMPEG_EVIDENCE_DIR"] = str(evidence_dir)
    try:
        completed = subprocess.run(
            [str(binary), str(rtsp.port), str(available_kib)],
            text=True, capture_output=True, env=environment, timeout=45,
        )
        snapshot = rtsp.snapshot()
    finally:
        rtsp.close()
    if completed.returncode:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(detail or "media ABI v3 harness failed")
    try:
        return json.loads(completed.stdout), snapshot, _read_encoder_evidence(evidence_dir)
    except json.JSONDecodeError as error:
        raise RuntimeError("media ABI v3 harness returned invalid JSON") from error


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ssh_wrapper", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--source-root", type=Path, default=PROJECT)
    args = parser.parse_args(argv)
    output = args.output_dir or Path(tempfile.mkdtemp(prefix="doorfast-multi-media-"))
    output.mkdir(parents=True, exist_ok=True)
    try:
        vm = _validate_vm(args.ssh_wrapper.resolve())
        available_kib = _available_memory_kib()
        harness = _build_harness(args.source_root.resolve(), output)
        report, rtsp, encoder_evidence = _run_harness(
            harness, output, available_kib
        )
        if rtsp["max_producer_count"] != 2:
            raise RuntimeError("host RTSP producer peak was not exactly two")
        if encoder_evidence != EXPECTED_ENCODER_EVIDENCE:
            raise RuntimeError("encoder pipe payload isolation was not satisfied")
        report["peak_active_encoders"] = rtsp["max_producer_count"]
        report["vm"] = vm
        report["rtsp"] = rtsp
        report["encoder_pipe_evidence"] = encoder_evidence
        (output / "acceptance.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n"
        )
        result = {
            "configured_capacity": report["configured_capacity"],
            "peak_active_encoders": report["peak_active_encoders"],
            "streams": report["streams"],
            "isolated_stop": report["isolated_stop"],
        }
        if result != {
            "configured_capacity": 2,
            "peak_active_encoders": 2,
            "streams": ["doorfast_gate_main", "doorfast_gate_side"],
            "isolated_stop": True,
        }:
            raise RuntimeError("multi-station media contract was not satisfied")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"multi-station media acceptance failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
