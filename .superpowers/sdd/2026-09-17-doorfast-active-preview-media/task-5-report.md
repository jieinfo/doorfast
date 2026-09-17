# Task 5 report: FFmpeg and RTSP publisher supervisor

## Commit

`563015d534dba45f0a201392835dfbd7f030edc5` — `fix: complete preview encoder supervision`

`9ef32f8` — `fix: harden preview encoder supervision`

This commit completes the supervisor introduced by `3a68a0b9a20ccb86b49ffc5ba1f736be7c8e404a`.

## RED evidence

The review tests were run before the implementation update:

```text
make -B build/doorfast-tests
tests/test_media_encoder.c: incomplete type 'struct df_media_encoder_config'
tests/test_media_encoder.c: no member named 'last_error'
tests/test_media_encoder.c: undeclared df_media_encoder_tick
tests/test_media_encoder.c: undeclared df_media_encoder_write_frame
tests/test_media_encoder.c: undeclared df_media_encoder_requires_restart
make: *** [build/doorfast-tests] Error 1
```

The failures matched the missing Task 5 interface and behavior rather than a test setup problem.

## Implemented behavior

- Starts FFmpeg directly with `fork` and `execvp`; no shell command is constructed.
- Builds the RTSP destination from validated fields and percent-encodes credentials.
- Supplies the complete low-latency software H.264 shape: `libx264`, `veryfast`, `zerolatency`, `yuv420p`, no B-frames, one-second GOP, `maxrate`, `bufsize`, repeated headers, RTSP/TCP, and `send_bye`.
- Supplies selected QSV and VAAPI codec/filter arguments; `auto` uses the already-supported software fallback when no selected hardware encoder is passed.
- Uses a nonblocking anonymous input pipe and copies at most one pending frame while backpressured.
- Returns `DF_MEDIA_ENCODER_RETRY` on `EAGAIN`; a retry must submit the same generation, timestamp, and frame length.
- Blocks and consumes only the write's generated `SIGPIPE`; `EPIPE` records a redacted error, invalidates the generation, and marks the encoder exited.
- Detects child exit with nonblocking `waitpid` in `df_media_encoder_tick`.
- Tracks the active dimensions for RTP-session restart decisions.
- Stops by closing stdin first, then uses bounded `SIGTERM` and `SIGKILL` phases with `waitpid(..., WNOHANG)` throughout.
- Keeps `last_error` generic and never copies the destination URL, password, argv, SDP, or payload into status text.

## Review fixes

- Avoids a blocking `sigwait` when the daemon inherited `SIGPIPE=SIG_IGN`; the regression suite now runs under that inherited disposition.
- Tracks source and encoded dimensions separately so a fixed output size neither restarts every frame nor hides source-size changes.
- Uses the documented 800 Kbps target with a 1.2 Mbps peak for the default configuration and derives the same 3:2 peak ratio for user-selected targets.
- Requires the capacity probe layer to resolve `auto` before starting the supervisor, and verifies QSV and VAAPI argv separately.
- Fails the child before `execvp` if stdin or `/dev/null` redirection cannot be established, preventing FFmpeg from inheriting daemon output streams.

## GREEN evidence

All commands exited zero:

```text
make -B test
make -B doorfast
make -B recorder
sh tests/test_package_manifest.sh
git diff --check
trap '' PIPE; ./build/doorfast-tests
```

The native suite covers direct argv execution, nonblocking pipe setup, complete RTSP arguments, credential percent encoding and redaction, stale generation rejection, dimension restart detection, `EAGAIN` retention, safe `EPIPE`, child-exit polling, idempotent cleanup, and `SIGTERM` to `SIGKILL` escalation.

The supervisor retains an `int` return and explicit timeout on `df_media_encoder_stop`. Child reaping can fail or time out, so downstream runtime integration needs an observable result; this is a deliberate refinement of the plan brief's `void` shorthand.

ASan and UBSan also passed:

```text
make CPPFLAGS='-D_DEFAULT_SOURCE -DDF_ALLOW_TEST_ROOT -fsanitize=address,undefined -fno-omit-frame-pointer' \
  CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -g -Isrc -Itests -Itests/support -I/opt/homebrew/Cellar/libpcap/1.10.6/include' \
  PCAP_LIBS='-L/opt/homebrew/Cellar/libpcap/1.10.6/lib -lpcap -fsanitize=address,undefined' test
```

A final normal `make -B test doorfast recorder` rebuild passed after the sanitizer run.

## Remaining risks

- Native tests execute a capture helper rather than a real FFmpeg/go2rtc session. QSV, VAAPI, RTSP `ANNOUNCE`/`RECORD`, and `TEARDOWN` still require the planned ImmortalWrt VM and HA go2rtc acceptance run.
- FFmpeg requires RTSP credentials in its destination argument. Doorfast never logs or returns that argument, but a local root user can inspect the child command line. The credentials file remains root-owned mode `0600`.
- Source-mode start defaults to the evidenced 480x640 shape when dimensions are omitted. The media module must pass dimensions from the first accepted JPEG and restart when later frames differ.
