# Task 6 report — protocol simulation and acceptance preparation

Date: 2026-09-30

## Scope and decision

The requested sequence is `03/04 → 03/84 → 03/03 → 03/83 → heartbeat → 03/02 → 03/82`, with station isolation, busy/timeout handling, and audio cleanup. Existing host tests already exercise the individual protocol and lifecycle invariants. The existing VM runner (`tests/run_gvs_vm_ubus_call.py`) only drives the legacy global `answer`/`hangup` API against one synthetic peer and intentionally uses an in-memory sender. It has no safe hook for injecting three independent station replies or heartbeat frames, and the HTTP runner is for PCM lifecycle only. A new runner would therefore require inventing integration hooks and was not added.

## Existing coverage audit

The core suite contains exact command/payload tests for preview, answer, and hangup; station and generation rejection tests; global call-lock and busy tests; confirmation timeout tests; keepalive ownership tests; and audio buffer/transmit cleanup tests. Multi-station media tests cover independent station sessions, preemption, capacity busy behavior, stale callbacks, and producer teardown. The HA checkout contains fixture isolation and audio cleanup tests plus the full station entity/client suite.

## Commands and results

Core checkout (`7d6c53b`):

* `make -B test` — passed (build and `build/doorfast-tests`, exit 0).
* `python3 -m unittest discover -s tests` — 25 tests passed.
* Shell smoke tests: all non-VM scripts passed. The HTTP smoke fixture was aligned with the POST-only hangup contract in commit `80b28ca`. `tests/test_vm_active_host.sh` remains unavailable because no VM is running on `127.0.0.1:2222`.

HA checkout (`6f8c113`):

* `python3 -B -m unittest tests.test_e2e_runner tests.test_release_metadata` — 7 passed.
* `node --test tests/js/*.mjs` — 12 passed.
* `python3 -m compileall -q custom_components/doorfast doorfast_ha_e2e run_acceptance.py` — passed.
* `python3 -B -m unittest discover -s tests` — 243 passed after isolating the fake entity registry fixtures in commit `eb312b3`.

No command sent frames to a real device. No persistent station configuration was changed.

## Remaining acceptance gate

Run the three-station sequence on the real or explicitly instrumented VM transport only after a per-station reply injection hook exists. During physical acceptance capture UDP 8300, 8302, and 8303, hold each call for at least 10 seconds, and verify no audio/keepalive tasks remain after `03/82` or local hangup timeout.
