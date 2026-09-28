# HA and Doorfast preview continuity: local gate

Date: 2026-09-28. This report covers local evidence only; release acceptance is pending.

## Checked revisions and worktrees

- Doorfast: `755378c1d9c25d30e73c50722dc68bd4f87aec6d` on `codex/preview-continuity-design`. The worktree was clean before verification.
- HA integration: `e999c526a41359c80f9ba18f8d3d4e11439bc429` on `codex/release-ha-v0.3.14`. The worktree was clean before and after verification.
- Relative to each branch's merge-base with `origin/main`, Doorfast changes 23 files and HA changes 6 files. The changed-file lists contain no PCAP, credential, token, or environment-secret file. A scan of added and removed diff lines found no apparent literal credential or private key. The only PCAP reference is the specification's capture-command example and existing SHA-256 evidence; no capture content was committed. This pattern scan does not prove the absence of every possible secret.

## Local verification

| Gate | Result |
| --- | --- |
| Doorfast `make test && ./build/doorfast-tests` | Exit 0; `make test` reported the target up to date, and the test binary exited 0. `tests/test_main.c` registers 345 C test calls. |
| HA `python3 -m unittest discover -s tests` | Exit 0; 231 tests ran in 4.386 seconds, `OK`. |
| `git diff --check` in each worktree | Exit 0 with no output. |
| `git diff --check $(git merge-base origin/main HEAD) HEAD` in each worktree | Exit 0 with no output. |

Local tests exercise short-stream retry and publication cleanup on Doorfast, and HA lease ownership, offer error classification, and WebRTC cleanup across source gaps. They do not demonstrate a real browser decoding frames from any physical station.

## External gates not run

- No PR was created and no PR Actions result was available. Neither branch was pushed for this task.
- No PR artifact was installed; no HA HACS update, real-device login, or physical capture was performed. HA HACS update remains a user action after the approved release path.
- The required ten complete browser ICE/decoder rounds for each of station 1, station 2, and B2 remain unrun. There are no new measurements of publication and attempt generations, RTSP producer, `03/02`/`03/50`, true decoded frames, reconnect prompt frames, first-frame latency, same-session recovery, or post-close `active_sessions` and `active_encoders`.
- Acceptance requires at least 9 of 10 rounds per station to decode a real frame within 60 seconds, at least 90% recovery after device-initiated end in the same browser session within 60 seconds, and zero active sessions and encoders after closure. Prompt frames do not count as real-frame success. No acceptance claim, merge, or release is justified by this local gate alone.
- The exact exception type and call site for station 1's earlier pre-answer `unknown_error` remain unobserved in a real HA/browser run. Unit tests verify classified paths but cannot identify that historical call site. Device busy responses, RTSP producer state, and HA signaling must be recorded separately during acceptance.

The existing diagnostic PCAP SHA-256 values are in the design specification. This report contains no PCAP payload, password, or token.
