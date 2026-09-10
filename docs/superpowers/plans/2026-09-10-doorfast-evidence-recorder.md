# Doorfast Evidence Recorder Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an independent recorder that keeps bounded local packet and metadata evidence under `/mnt/doorfast` without affecting Doorfast parsing or bridge forwarding.

**Architecture:** A separate `doorfast-recorder` process opens the selected capture interface once, writes truncated recent packets to one fixed ring, writes validated `GVSGVS` control frames to a second ring, and appends bounded local metadata logs. An atomic status file communicates recorder health; low space stops recording rather than networking.

**Tech Stack:** C17, libpcap savefiles, `statvfs(3)`, atomic rename, procd, existing GVS Ethernet/UDP/frame validators.

**Spec:** `docs/superpowers/specs/2026-09-10-doorfast-inline-observation-deployment-design.md`

## Global Constraints

- Execute only after `2026-09-10-doorfast-inline-preflight.md` is complete.
- Target x86_64 ImmortalWrt 25.12.1 and reuse `/etc/config/doorfast-deployment`.
- Recent ring maximum: 14336 MiB; control ring maximum: 8192 MiB; local metadata logs maximum: 1024 MiB.
- Stop all new evidence writes before available space falls below 6144 MiB.
- Recent packets are truncated to 256 bytes; validated control packets are capped at 2048 bytes.
- Raw payload bytes never appear in text logs; passwords, tokens, and update credentials are never recorded.
- Recorder failure cannot stop `/usr/sbin/doorfast`, restart networking, or alter `br-door`.
- Recording defaults disabled and never falls back to overlay or tmpfs.

---

### Task 1: Preserve packet timestamps and original lengths

**Files:**
- Modify: `src/capture.h`
- Modify: `src/capture.c`
- Modify: `src/runtime_service.c`
- Modify: `tests/test_capture.c`

**Interfaces:**
- Consumes: the existing libpcap handle.
- Produces: `int df_capture_next_record(struct df_capture *, struct df_capture_record *)` while retaining `df_capture_next()` as a compatibility wrapper.

- [ ] **Step 1: Write the failing capture-record test**

```c
struct df_capture_record {
    const uint8_t *data;
    size_t captured_length;
    size_t original_length;
    uint64_t wall_seconds;
    uint32_t wall_microseconds;
};
```

Use the existing deterministic capture seam to assert that a record preserves `caplen`, wire `len`, and timeval independently. Add a wrapper test proving `df_capture_next()` still returns the same data and captured length.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: compilation fails because `df_capture_record` and `df_capture_next_record()` do not exist.

- [ ] **Step 3: Implement the record API and compatibility wrapper**

Copy scalar header fields before the next libpcap call invalidates them. Clear the output before reading and preserve the current timeout/error enum values. Change `runtime_service.c` to use the new record API without changing parser behavior.

- [ ] **Step 4: Run all C tests**

Run: `make test`

Expected: all existing parser/runtime tests and new timestamp tests pass.

- [ ] **Step 5: Commit capture metadata support**

```bash
git add src/capture.c src/capture.h src/runtime_service.c tests/test_capture.c
git commit -m "feat: preserve capture record metadata"
```

### Task 2: Implement fixed-size PCAP segment rings

**Files:**
- Create: `src/pcap_ring.h`
- Create: `src/pcap_ring.c`
- Create: `tests/test_pcap_ring.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `struct df_capture_record` and explicit ring configuration.
- Produces: `df_pcap_ring_init`, `df_pcap_ring_write`, `df_pcap_ring_status`, and `df_pcap_ring_close`.

- [ ] **Step 1: Write failing rotation and recovery tests**

```c
struct df_pcap_ring_config {
    const char *directory;
    const char *prefix;
    uint32_t segment_count;
    uint64_t segment_bytes;
    uint32_t snaplen;
};

struct df_pcap_ring_status {
    uint32_t active_slot;
    uint32_t completed_segments;
    uint64_t bytes_written;
};
```

Use a temporary test directory. Configure three 256-byte segments, write literal Ethernet records, and assert rotation produces only `recent-000.pcap` through `recent-002.pcap` plus one active `.partial`. Reopen and verify the oldest completed slot is replaced next. Confirm the PCAP record header contains the original wire length but a caplen no larger than configured snaplen.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: missing ring functions fail compilation.

- [ ] **Step 3: Implement atomic bounded rotation**

Use classic little-endian PCAP with Ethernet link type. The active slot is `<prefix>-NNN.partial`; flush and close it before renaming to `.pcap`. Slot selection is numeric and bounded, never based on untrusted filenames. Reject symlinks, non-directories, counts below two, zero sizes, path traversal, and multiplication overflow. A failed write leaves the prior completed segments untouched.

- [ ] **Step 4: Verify recovery, bounds, and sanitizers**

Run:

```bash
make test
make clean
make test CC=clang CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Expected: all tests pass, and a deliberately truncated `.partial` is ignored on reopen.

- [ ] **Step 5: Commit the PCAP ring**

```bash
git add Makefile src/pcap_ring.c src/pcap_ring.h tests/test_pcap_ring.c tests/test_main.c
git commit -m "feat: add bounded pcap segment rings"
```

### Task 3: Classify recent and control evidence

**Files:**
- Create: `src/evidence_classifier.h`
- Create: `src/evidence_classifier.c`
- Create: `tests/test_evidence_classifier.c`
- Modify: `src/gvs_packet.h`
- Modify: `src/gvs_packet.c`
- Create: `tests/test_gvs_packet.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: Ethernet capture records up to 2048 bytes, including packets whose wire length exceeds captured length.
- Produces: `int df_gvs_inspect_udp_prefix(const uint8_t *, size_t, struct df_udp_prefix *)` and `int df_evidence_classify(const struct df_capture_record *, struct df_evidence_classification *)`.

- [ ] **Step 1: Write literal classification tests**

```c
struct df_evidence_classification {
    bool recent;
    bool valid_control;
    uint16_t source_port;
    uint16_t destination_port;
    uint8_t family;
    uint8_t opcode;
    size_t control_length;
};

struct df_udp_prefix {
    uint16_t source_port;
    uint16_t destination_port;
    size_t payload_offset;
    size_t captured_payload_length;
    size_t declared_payload_length;
    bool payload_complete;
};
```

Test a valid 42-byte `GVSGVS` control frame, a VLAN-tagged frame, a UDP/8303 non-control media-like packet truncated after 256 bytes, an unrelated UDP packet, a packet truncated before its UDP header, and a declared-length mismatch. Hand-derive expected family/opcode/length values. The 256-byte media prefix must be recent but not valid control.

- [ ] **Step 2: Run tests and verify RED**

Run: `make test`

Expected: classifier symbols are missing.

- [ ] **Step 3: Implement classification through production parsers**

Add `df_gvs_inspect_udp_prefix()` beside the existing packet extractor. It validates Ethernet, optional single VLAN, IPv4 header, UDP header, and declared lengths, but may return an incomplete payload prefix once the UDP header is fully captured. Use it only to decide `recent` for the four configured UDP ports. Continue using `df_gvs_extract_control_payload()` plus `df_gvs_frame_parse()` for `valid_control`, which requires the complete datagram, public header, and exact declared length. Media-like packets never enter the control ring.

- [ ] **Step 4: Run parser and classifier regression suites**

Run: `make test`

Expected: malformed packets remain rejected and all existing GVS parsing tests pass.

- [ ] **Step 5: Commit evidence classification**

```bash
git add Makefile src/evidence_classifier.c src/evidence_classifier.h src/gvs_packet.c src/gvs_packet.h tests/test_evidence_classifier.c tests/test_gvs_packet.c tests/test_main.c
git commit -m "feat: classify bounded gvs evidence"
```

### Task 4: Add a bounded local metadata log

**Files:**
- Create: `src/evidence_log.h`
- Create: `src/evidence_log.c`
- Create: `tests/test_evidence_log.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: capture timestamps, interface name, parsed MAC/IP/ports, GVS address/family/opcode/length, session generation, and health events.
- Produces: `df_evidence_log_open`, `df_evidence_log_append`, `df_evidence_log_status`, and `df_evidence_log_close`.

- [ ] **Step 1: Write failing local-fidelity and rotation tests**

Define `struct df_evidence_log_record` with only wall/monotonic time, interface, MAC, IP, UDP ports, GVS logical addresses, family, opcode, length, session generation, and health-state fields. Create a 64-byte per-line test segment and assert literal JSON Lines preserve exact device fields and rotate within the configured count. The API accepts neither arbitrary key/value maps nor binary payload pointers, so credential and raw-payload fields cannot be serialized through it.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: evidence log API is absent.

- [ ] **Step 3: Implement local-fidelity JSON Lines rotation**

Use fixed keys only; JSON-escape all string values. Configure 64 segments of 16 MiB for a 1024 MiB maximum. Use `.partial` and atomic rename like the PCAP ring. Store files mode `0600` and directories mode `0700`. Do not accept an arbitrary map of fields, which prevents accidental secret expansion.

- [ ] **Step 4: Run tests and inspect literal output**

Run: `make test`

Expected: exact MAC/IP/GVS identity values survive, secrets and raw packet bytes cannot enter text output, and rotation remains bounded.

- [ ] **Step 5: Commit local evidence logging**

```bash
git add Makefile src/evidence_log.c src/evidence_log.h tests/test_evidence_log.c tests/test_main.c
git commit -m "feat: add local fidelity evidence log"
```

### Task 5: Build the independent recorder process

**Files:**
- Create: `src/recorder_main.c`
- Create: `src/evidence_recorder.h`
- Create: `src/evidence_recorder.c`
- Create: `tests/test_evidence_recorder.c`
- Modify: `Makefile`
- Modify: `package/doorfast/Makefile`
- Modify: `package/doorfast/files/doorfast.init`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: deployment config, capture records, classifier, rings, and log.
- Produces: `/usr/sbin/doorfast-recorder` and atomic `/var/run/doorfast-recorder.status`.

- [ ] **Step 1: Write failing orchestration tests**

Inject fake capture records and a fake free-space provider. Assert one valid control packet creates one 256-byte-capped recent record and one full control record; a media-like packet creates only a recent record; crossing the 6144 MiB reserve changes state to `space_guard` and performs no further writes. Killing/restarting the recorder must preserve completed segments. Add a CLI test proving `--self-test` rejects every path except an empty `/tmp/doorfast-recorder-selftest` directory and never opens a network interface.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: recorder types and binary target are absent.

- [ ] **Step 3: Implement recorder orchestration and status**

Use 112 recent segments of 128 MiB and 64 control segments of 128 MiB. Check `statvfs()` before each rotation and at least every 1000 packets. Write a fixed status document containing state, packet counters, segment counters, bytes, last packet wall time, last rotation wall time, and available bytes; update via a temporary file and rename. A capture or write error exits only the recorder process with nonzero status. Implement `--self-test /tmp/doorfast-recorder-selftest` as a network-free path that writes built-in synthetic records through two fixed 4 KiB rings, verifies rotation and recovery, removes only its own exact files, and exits; no other path or option activates reduced limits.

- [ ] **Step 4: Add a separate disabled procd instance**

Start `/usr/sbin/doorfast-recorder --config /etc/config/doorfast-deployment` only when both deployment `enabled` and `recording_enabled` are `1`. It must not be a dependency of the main Doorfast process. Package both binaries, bump core APK release, and keep networking untouched.

- [ ] **Step 5: Run all host and manifest tests**

Run:

```bash
make test
sh tests/test_main_cli.sh
sh tests/test_package_manifest.sh
```

Expected: all pass and package manifests contain both binaries without raw capture files.

- [ ] **Step 6: Commit recorder integration**

```bash
git add Makefile src/recorder_main.c src/evidence_recorder.c src/evidence_recorder.h tests/test_evidence_recorder.c package/doorfast/Makefile package/doorfast/files/doorfast.init tests/test_package_manifest.sh
git commit -m "feat: add passive evidence recorder"
```

### Task 6: Export a sanitized evidence bundle explicitly

**Files:**
- Create: `src/evidence_export.h`
- Create: `src/evidence_export.c`
- Create: `src/evidence_export_main.c`
- Create: `tests/test_evidence_export.c`
- Modify: `Makefile`
- Modify: `package/doorfast/Makefile`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: one administrator-selected completed control PCAP segment under `/mnt/doorfast`.
- Produces: `/usr/sbin/doorfast-evidence-export` and a root-only sanitized directory containing `manifest.json` and `events.jsonl`; it never exports packet bytes.

- [ ] **Step 1: Write failing deterministic anonymization tests**

Use a literal control PCAP containing two devices and two sessions. Assert the exported JSON uses relative microseconds from the first packet, stable encounter-order identifiers `mac-1`, `ip-1`, and `gvs-1`, while preserving UDP ports, family, opcode, declared length, direction, and session grouping. Assert no original MAC, IP, logical address, random/encryption header field, payload byte sequence, source path, password, or token occurs in either output file.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: export symbols and binary are absent.

- [ ] **Step 3: Implement metadata-only export**

Require a regular, non-symlink `.pcap` file below the configured control-ring directory and an unused output name below `/mnt/doorfast/exports`. Parse only validated control frames through production parsers. Build pseudonym tables in memory, write fixed-schema JSON through temporary files, set directory mode `0700` and file mode `0600`, then rename atomically. Never copy packet bytes or write a reversible mapping file.

- [ ] **Step 4: Run export and package tests**

Run:

```bash
make test
sh tests/test_package_manifest.sh
```

Expected: exact local evidence remains untouched, sanitized output contains no original identifier or payload, and malformed/truncated inputs create no completed export.

- [ ] **Step 5: Commit explicit export support**

```bash
git add Makefile src/evidence_export.c src/evidence_export.h src/evidence_export_main.c tests/test_evidence_export.c package/doorfast/Makefile tests/test_package_manifest.sh
git commit -m "feat: export sanitized evidence metadata"
```

### Task 7: Verify real rotation in the isolated VM

**Files:**
- Create: `tests/run_doorfast_vm_recorder.py`
- Create: `docs/doorfast-evidence-storage.md`

**Interfaces:**
- Consumes: built APK, existing VM SSH helper, loopback UDP fixture injector.
- Produces: repeatable evidence that recording is bounded and independent.

- [ ] **Step 1: Write the VM acceptance script before installing the new APK**

The script snapshots routes, firewall rules, bridge state, and Doorfast PID. It first runs `doorfast-recorder --self-test /tmp/doorfast-recorder-selftest`, a network-free mode restricted to that exact empty temporary directory and fixed 4 KiB rings, to force rotation and malformed-write recovery. It then uses the normal deployment config to inject control and media-like fixtures and compares network state and the main Doorfast PID. The self-test deletes only files it created beneath its exact temporary directory and is never started by procd.

- [ ] **Step 2: Run against the previous APK and verify RED**

Expected: `/usr/sbin/doorfast-recorder` is missing.

- [ ] **Step 3: Build and install the new APK**

Use the repository GitHub Actions workflow and install the x86_64 artifact in the isolated ImmortalWrt VM. Do not poll the workflow continuously; run acceptance after completion is reported.

- [ ] **Step 4: Run VM acceptance**

Run:

```bash
python3 -B tests/run_doorfast_vm_recorder.py /absolute/path/to/vm/ssh.sh
```

Expected: both rings rotate, control filtering is correct, reserve guard stops writes, files are root-only, main Doorfast stays alive, and network/firewall snapshots are unchanged.

- [ ] **Step 5: Commit acceptance documentation**

```bash
git add tests/run_doorfast_vm_recorder.py docs/doorfast-evidence-storage.md
git commit -m "test: validate bounded evidence recording"
```
