# Doorfast Active Event Push Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a local, authenticated, generation-bound event stream from Doorfast to `doorfastforha` while preserving polling fallback.

**Architecture:** Doorfast publishes bounded newline-delimited JSON events over `/var/run/doorfast/events.sock`; the HA integration consumes and deduplicates them, then refreshes authoritative status. The producer never blocks the call loop and the HTTP CGI is unchanged.

**Tech Stack:** C17, POSIX Unix sockets, LuCI/OpenWrt init scripts, Python Home Assistant integration, JSON Lines.

**Spec:** `docs/superpowers/specs/2026-09-15-doorfast-active-event-push-design.md`

## Global Constraints

- Events are advisory; `/api/v1/status` remains authoritative.
- Only local Unix socket clients may consume events.
- Every event carries `schema_version`, `event_id`, `event`, `generation`, and `timestamp_ms`.
- A client must ignore duplicate `(generation,event)` pairs and stale generations.
- A producer queue is bounded at 64 events per client and never blocks the runtime loop.
- Existing ubus, HTTP, video, audio, PCM, and passive-mode behavior remain compatible.

### Task 1: Doorfast event publisher

**Files:**
- Create: `src/event_stream.h`, `src/event_stream.c`
- Modify: `src/runtime_service.c`, `src/Makefile` or package build source list
- Test: `tests/test_event_stream.c`, `tests/test_main.c`

**Interfaces:**
- `df_event_stream_init(struct df_event_stream *, const char *path)`
- `df_event_stream_publish(struct df_event_stream *, const char *event, uint64_t generation, uint64_t now_ms)`
- `df_event_stream_process(struct df_event_stream *)`
- `df_event_stream_stop(struct df_event_stream *)`

- [ ] Write failing tests for exact JSON fields, unknown-event rejection, 64-entry bound, and non-blocking disconnect handling.
- [ ] Run `make -B test`; confirm the new symbols are missing and tests fail.
- [ ] Implement a Unix listener with `AF_UNIX/SOCK_STREAM`, `0660` mode, one bounded output queue per client, and nonblocking writes.
- [ ] Publish events only after accepted call, talking transition, observed hangup, timeout, and preemption transitions; use existing session generation.
- [ ] Run focused and full C tests.
- [ ] Commit as `Add bounded local Doorfast event stream`.

### Task 2: Package lifecycle and socket permissions

**Files:**
- Modify: `package/doorfast/Makefile`, `package/doorfast/files/doorfast.init`
- Create: `package/doorfast/files/doorfast-group`
- Test: `tests/test_package_manifest.sh`, `tests/test_doorfast_init.sh`

**Interfaces:**
- Socket path remains `/var/run/doorfast/events.sock`.
- Init creates `doorfast` group and starts Doorfast with the event stream enabled.

- [ ] Add failing manifest assertions for the group, socket path, and `0660` mode.
- [ ] Implement procd directory creation and group ownership without changing network configuration.
- [ ] Verify package scripts with shell syntax and manifest tests.
- [ ] Commit as `Package Doorfast event stream permissions`.

### Task 3: HA event consumer

**Files:**
- Modify: `custom_components/doorfast/__init__.py`, `client.py`, `const.py`
- Create: `custom_components/doorfast/event_stream.py`
- Test: `tests/test_event_stream.py`, `tests/test_init.py`

**Interfaces:**
- `EventStreamConsumer(async def run(), async def stop())`
- Consumer callback receives validated `{event, generation, event_id, timestamp_ms}`.

- [ ] Write failing tests for valid parsing, duplicate suppression, stale generation rejection, reconnect, and refresh after delivery.
- [ ] Implement a local transport adapter configured by the Doorfast bridge address; retain five-second polling.
- [ ] Dispatch HA ring and latest-event signals only after status refresh succeeds.
- [ ] On EOF, retry with bounded backoff and trigger an immediate status refresh.
- [ ] Run the full HA test suite.
- [ ] Commit as `Consume Doorfast active events in Home Assistant`.

### Task 4: VM acceptance and documentation

**Files:**
- Create: `tests/run_doorfast_vm_event_stream.py`
- Modify: `docs/current-capability-status.md`, `README.md`, `doorfastforha/README.md`

- [ ] Install the new APKs in the isolated VM after Actions completion.
- [ ] Verify socket owner/mode, event delivery, duplicate suppression, daemon restart, and polling fallback.
- [ ] Verify network/firewall configuration hashes are unchanged.
- [ ] Update capability status to distinguish host tests, VM event delivery, and missing physical-device evidence.
- [ ] Commit acceptance evidence and documentation.
