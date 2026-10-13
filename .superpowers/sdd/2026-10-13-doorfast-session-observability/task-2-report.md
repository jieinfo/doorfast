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
