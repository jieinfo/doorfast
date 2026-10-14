# Task 2 report: guarded outbound preview-to-call media handoff

## Scope

The media manager now exposes a separately versioned outbound handoff extension. A request with an exact station/media key performs eligibility checks against the live monitor, confirms the station and route/capacity, and only then promotes the existing preview in place. The public media generation, encoder/publisher pipeline, queue, and viewer ownership remain unchanged. The extension also exposes explicit bind-control, rollback, and release operations for the runtime coordinator.

The existing v3 API table and layout are unchanged. Incoming calls with a non-zero request generation continue through `df_media_session_manager_incoming_call` and retain their prior media binding behavior. A dynamically loaded module without the optional handoff extension is rejected by the new runtime path; test-only in-process API fixtures retain the legacy fallback so existing ABI characterization tests remain valid.

## TDD evidence

### RED (old production behavior)

Command:

```text
make -B build/doorfast-tests && build/doorfast-tests --runtime-media-module --media-session-manager --gvs-monitor
```

The new real manager/monitor assertion failed against the old implementation: an unconfirmed preview accepted CALL with `DF_OK`, changed the session purpose to CALL, changed the public generation from `1` to `2`, and cleared the viewer. The failure was an assertion failure in `tests/test_media_session_manager.c`; the process later terminated because the intentionally violated ownership assertions continued. This established that the test caught the production branch rather than a fixture construction error.

### GREEN

The same selector now passes:

```text
runtime media module, media session manager, and gvs monitor tests passed
exit=0
```

The full suite was also run through `make -B build/doorfast-tests` and completed with exit status 0. The new selector is registered in `tests/test_main.c` and runs runtime media-module, media-session-manager, and GVS monitor regressions together.

## Covered behavior

- No current monitor confirmation: outbound CALL is rejected and the preview remains active.
- Exact station/media generation: stale or missing keys are rejected before mutation.
- Current matching `03/84`: preview is promoted in place, retaining generation and viewer state.
- Wrong peer/address, retry/stop states, expired first-frame window, source stall, unavailable route, insufficient capacity, and memory failure remain eligibility failures.
- A second station's active CALL blocks the handoff; duplicate handoff on the same station is idempotent.
- Explicit rollback restores preview purpose; release stops owned resources and records a terminal result.
- Outbound handoff media monitoring does not emit a second preview keepalive owner.

## Interface decisions for Task 3

Use `df_media_module_outbound_upgrade_api_v1` through the optional symbol `df_media_module_outbound_upgrade_api_v1`. Its operation enum is `CHECK`, `ROLLBACK`, `RELEASE`, and `BIND_CONTROL`; all calls require the exact station/media key. `CHECK` performs eligibility only. A successful `df_media_module_api_v3.start(..., CALL, request_generation=0, ...)` then performs the in-place promotion. Incoming `request_generation != 0` remains separate. Task 3 should bind the real control generation with `BIND_CONTROL`, use `ROLLBACK` if control submission fails before send, and use `RELEASE` for terminal media cleanup.

## Constraints and concerns

The extension is optional by design so old shared objects cannot execute the new runtime's outbound CALL path. In-process test fixtures without a dynamic-library handle retain a compatibility fallback used only by existing ABI tests; the production dynamic-loader path fails closed when the extension is absent.
