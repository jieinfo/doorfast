# Task 2 report — call terminal snapshot and fault classification

## Inherited work

Resumed the dirty `codex/preview-startup-state` worktree at base `525a179` with the Task 2 implementation already present in the tracked files. No reset, revert, stash, clean, remote action, installation, or live action was performed. The inherited edits covered the call terminal/fault structs, transition classification, relative timer status, runtime reset API, receive hangup attribution, runtime deadline binding, ubus status fields, and focused call-control tests.

The prior agent's reported RED/GREEN history was unavailable, so this report records only fresh verification from the resumed worktree.

## Corrections made

- Kept unmatched, malformed, stale, or late acknowledgement frames from being classified as `confirmation_rejected`; R2 requires refusal to have explicit evidence, which this receive path does not provide.
- Preserved the existing local hangup confirmation flow while retaining peer hangup attribution from an observed inbound terminal transition.
- Kept dispatch failures/timeouts visible in `call.fault` after the dispatch state becomes terminal, including when the session is no longer active.

## Verification

Commands run from the worktree:

```text
make -B test
make -B build/doorfast-tests
```

Both commands rebuilt and ran `build/doorfast-tests` successfully (exit code 0). Output includes the existing media/audio diagnostic lines; no compiler warnings or test assertion failures occurred.

## Limitations

- No new runtime_ubus serializer test was added; the existing full test binary and compile path cover the changed ABI and handler code, while focused call-control tests cover timeout, peer hangup, and runtime reset snapshots.
- `runtime_reset` is an explicit caller API. The daemon's existing capture-error cleanup path was not reclassified as a runtime reset because it does not prove a runtime identity change (R5).
- The current protocol does not expose an explicit peer refusal distinct from malformed/late acknowledgements, so `confirmation_rejected` remains unused by this path rather than being inferred.

## Review fixes

- Added a generation-bound local hangup intent at control submission. A successful
  `03/82` confirmation now applies the local session abort in the control layer,
  cancels its deadline, and records `local_hangup`; dispatch timing and the
  existing runtime ACK behavior are unchanged. Failed, timed-out, cancelled, or
  stale submissions clear the intent without claiming a local hangup.
- Classified an active session ended by the handshake miss limit as
  `heartbeat_timeout` for both `end_reason` and `fault`, preserving station and
  generation attribution.
- Kept terminal fault snapshots authoritative for the terminal generation so a
  later dispatch cancellation cannot replace peer/session timeout, heartbeat, or
  runtime-reset fault state. Dispatch failures remain visible when no terminal
  snapshot exists.
- Added focused coverage for local hangup confirmation, heartbeat timeout,
  runtime-reset terminal-fault preservation, and dispatch failure fault naming.

The review-fix RED run was a fresh `make -B build/doorfast-tests` that failed to
compile because the new heartbeat enum names were not yet defined. After the
minimal implementation, both required forced commands passed:

```text
make -B test
make -B build/doorfast-tests
git diff --check
```

Both test commands rebuilt and ran `build/doorfast-tests` with exit code 0. The
existing media/audio diagnostic lines were emitted; no compiler warnings or
assertion failures occurred.
