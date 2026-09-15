# Task 4 report: production CGI, ubus adapter, and package lifecycle

Status: complete with one verification limitation noted below.

## Commits

- `a58417970bf9b36e513fd2620f31f9d39b97e413` — Package the HTTP PCM bridge. Added the initial helper, package install, release 39, init cleanup, and host target.
- `a2557a34b11f8c7e3c7e264193df2fb7e82c5361` — Complete ubus status adapter and runtime query. Added the first real ubus status callback and corrected the public query key to `runtime`.
- `3671cd7e1ad625c0d099c9ef50a7de3b739f5c59` — Finish the packaged HTTP PCM bridge. Recovered and completed the CGI program, corrected ubus parsing and cleanup, completed host and package build wiring, and added process, dispatch, lifecycle, and manifest tests.

The report itself is committed separately after the implementation SHA above was available.

## Files

- `src/pcm_http_ubus.h` and `src/pcm_http_ubus.c`: fixed `doorfast` / `status` ubus adapter and PCM ingress callback.
- `src/pcm_http_main.c`: guarded production/test program, exact route and query parsing, CGI environment mapping, fixed production paths, complete random reads, absolute monotonic pacing, bounded JSON rendering, and compile-time-only test callbacks.
- `Makefile`: deterministic `pcm-http-test` host target with only the required test-mode sources.
- `package/doorfast/Makefile`: release 39 helper compile/link and `/usr/sbin/doorfast-pcm-http` install, including the required `runtime_id.c` link dependency.
- `package/doorfast/files/doorfast-http.sh`: exact three-route early `exec` before shell body handling.
- `package/doorfast/files/doorfast.init`: volatile bridge state removal after the enabled check and before daemon launch.
- `tests/test_pcm_http_cli.sh`: process-level malformed request, response, non-reflection, failure metadata, and exact binary-submit coverage.
- `tests/test_doorfast_http.sh`: temporary-copy helper substitution, three exact route checks, adjacent-path rejection, `exec` termination, and binary stdin preservation.
- `tests/test_doorfast_init.sh`: disabled-service exclusion and cleanup-before-`procd_open_instance` ordering.
- `tests/test_package_manifest.sh`: release, helper installation, no acceptance artifact, and required compile/link input assertions.

## Red/green evidence

The first run of `sh tests/test_pcm_http_cli.sh` exited 1 at its first exact status assertion. The recovered helper emitted `Status: 405` without `Method Not Allowed`; it also still had a fixed test callback that reported a non-talking call and no configurable test sink. This was the expected failure for the missing process contract.

The new shell-dispatch and init tests passed on their first run because the interrupted worktree already contained the useful early route `exec` and init cleanup edits. Those edits were preserved as required. The tests add observable regression coverage for their behavior rather than replacing or discarding them.

After implementation, the CGI process test passed with exact reason-bearing status lines, fixed JSON, `Content-Type: application/json`, `Cache-Control: no-store`, and a 320-byte payload containing NUL bytes delivered byte-for-byte to the test sink. The same test confirms the full body and a raw query marker never appear in stdout or stderr.

## Requirement review

- The ubus callback parses the top-level response with `blob_data` / `blob_len`, then parses only `call` and `audio_tx` nested tables with blob-message accessors. It checks parser return values, required fields, exact policy types, a lowercase 16-character runtime ID, a call-session string shorter than the destination, boolean range, nonzero 64-bit generations, and equal call/audio transmit generations. The result is copied out before `ubus_free`; lookup, invoke, parse, and timeout failures all free the connection.
- Production uses only object `doorfast`, method `status`, state `/tmp/doorfast-pcm-http.state`, and socket `/var/run/doorfast-audio.sock`. Test state and sink settings exist only inside `DF_PCM_HTTP_TEST_PROGRAM` preprocessing branches.
- The CGI maps only `/api/v1/audio/session`, `/api/v1/audio/submit.pcm`, and `/api/v1/audio/session/end`. It accepts only `runtime`, `generation`, and the submit-only `sequence`, rejecting missing, duplicate, empty, oversized, malformed, and unknown query items.
- `REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_TYPE`, `CONTENT_LENGTH`, and `HTTP_X_DOORFAST_AUDIO_SESSION` map directly into the core request, with `STDIN_FILENO` retained as the body descriptor.
- Linux production pacing uses `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)`; the macOS host-test fallback recomputes against the same absolute monotonic deadline after interruptions.
- `/dev/urandom` reads retry `EINTR` and require every requested byte plus a successful close.
- JSON is rendered into a fixed 512-byte buffer from bounded response fields. Success and authoritative conflict/failure metadata follow the design contract; raw request data is never rendered.
- The program body compiles only for `DF_PCM_HTTP_PROGRAM` or `DF_PCM_HTTP_TEST_PROGRAM`, preventing package wildcard daemon and recorder builds from acquiring a second `main`.
- The installed CGI retains its fixed helper assignment. Tests replace only that assignment in a temporary copy and prove all three routes preserve binary stdin and terminate through the helper.
- No Task 5 acceptance binary, VM runner, workflow artifact, or package installation was added.

## Exact verification results

All commands below exited 0 on 2026-09-15:

```text
make clean && make test
make pcm-http-test
sh tests/test_pcm_http_cli.sh
sh tests/test_doorfast_http.sh
sh tests/test_doorfast_init.sh
sh tests/test_package_manifest.sh
git diff --check
```

The normal C suite executed its complete 164-test registry. The required sanitizer command also exited 0 with no AddressSanitizer or UndefinedBehaviorSanitizer diagnostics:

```text
make clean && make CC=clang CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

Additional strict compilation checks exited 0 for `pcm_http_main.c` under `DF_PCM_HTTP_PROGRAM` and under the recorder/wildcard macro set. Symbol inspection found `main` only in the intended production object, and string inspection found no test-only environment setting names in that object. Shell syntax checks passed for every modified shell test and CGI script.

## Concern

This macOS host does not contain the ImmortalWrt SDK or libubus development headers, so the actual target package cross-compile and a live ubus invocation were not executable here. The package command is covered by manifest assertions, includes the previously missing `runtime_id.c` dependency, and the ubus API usage was checked against the existing project adapter patterns and libubox interfaces. VM-level execution belongs to Task 5 and was intentionally not added.
