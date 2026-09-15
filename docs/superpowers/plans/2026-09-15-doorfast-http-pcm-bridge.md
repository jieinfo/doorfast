# Doorfast HTTP PCM Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded, generation-bound HTTP microphone ingress that safely forwards 20–100 ms PCM batches to Doorfast's existing private Unix audio socket.

**Architecture:** The daemon publishes a per-process runtime ID. A small C CGI helper owns strict request parsing, a locked producer lease and sequence state, full-body buffering, and 20 ms frame pacing; its production adapter reads authoritative status through ubus and sends through the existing PCM ingress serializer. The installed shell CGI only dispatches binary audio routes with `exec`, while a separately built, non-installed acceptance variant injects status and socket paths for VM tests.

**Tech Stack:** C17, POSIX file locking and monotonic clocks, libubus/libubox/blobmsg-json, Unix datagram sockets, POSIX shell, Python 3 VM acceptance, ImmortalWrt 25.12.1 x86_64 APK build.

**Spec:** `docs/superpowers/specs/2026-09-15-doorfast-http-pcm-bridge-design.md`

## Global Constraints

- Accept only 8 kHz, mono, signed 16-bit little-endian PCM in 320-byte frames.
- Accept one to five frames per request: exact lengths `320`, `640`, `960`, `1280`, or `1600` bytes.
- Bind every session and batch to a 16-character lowercase hexadecimal daemon `runtime_id`, a nonzero unsigned 64-bit call generation, a 32-character lowercase hexadecimal producer token, and a continuous unsigned 64-bit frame sequence.
- Permit one producer per runtime and generation with a 2,000 ms monotonic lease.
- Read the complete bounded request body before sending its first frame; never put PCM in a shell variable or log.
- Pace frames against absolute monotonic deadlines 20 ms apart and keep at most one HTTP request in flight on the client side.
- Keep the daemon's talking state, generation, audio-transmit state, queue, pacing, G.711 A-law encoding, and learned route checks authoritative.
- Store bridge state at `/tmp/doorfast-pcm-http.state`; reject symlinks and non-regular, wrong-owner, or non-`0600` files.
- The installed helper always uses `doorfast` ubus status and `/var/run/doorfast-audio.sock`; overrides exist only in the non-installed acceptance build.
- Preserve the existing CGI access boundary; the producer token coordinates ownership and does not replace HTTP authentication.
- Do not claim physical speaker interoperability until MT8157 replacement hardware validates it.

---

## File map

- `src/runtime_id.h`, `src/runtime_id.c`: generate and validate the daemon-instance identifier.
- `src/runtime_ubus.h`, `src/runtime_ubus.c`: retain the identifier for one daemon lifetime and expose it in `doorfast status`.
- `src/pcm_http.h`, `src/pcm_http.c`: protocol-neutral request validation, state-file ownership, producer lease, sequence handling, full-body reads, pacing, and structured responses.
- `src/pcm_http_ubus.h`, `src/pcm_http_ubus.c`: production-only authoritative ubus status adapter.
- `src/pcm_http_main.c`: CGI environment parser, production dependency wiring, and CGI response rendering; acceptance-only injected provider selected at compile time.
- `tests/test_runtime_id.c`, `tests/test_pcm_http.c`: deterministic C tests for identifiers, leases, races, body handling, sequence recovery, and pacing.
- `tests/test_pcm_http_cli.sh`, `tests/test_doorfast_http.sh`: CGI process and shell-dispatch boundary tests.
- `tests/run_doorfast_vm_pcm_http.py`: installed-path rejection checks and isolated valid-path VM acceptance.
- `package/doorfast/files/doorfast-http.sh`, `package/doorfast/files/doorfast.init`, `package/doorfast/Makefile`: route dispatch, volatile-state cleanup, helper build/install, and release bump.
- `.github/workflows/build-apk.yml`: host checks and non-installed musl acceptance-binary artifact.
- `docs/doorfast-http-bridge.md`, `docs/local-pcm-ingress.md`, `docs/current-capability-status.md`: public contract, trust boundary, recovery rules, and measured validation status.

### Task 1: Daemon runtime identity

**Files:**
- Create: `src/runtime_id.h`
- Create: `src/runtime_id.c`
- Create: `tests/test_runtime_id.c`
- Modify: `src/runtime_ubus.h`
- Modify: `src/runtime_ubus.c`
- Modify: `tests/test_runtime_ubus.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `/dev/urandom` through an injectable byte-fill callback.
- Produces: `DF_RUNTIME_ID_HEX_LENGTH`, `df_runtime_id_generate()`, `df_runtime_id_is_valid()`, and `df_runtime_ubus.runtime_id[17]`; the ubus root status field is named `runtime_id`.

- [ ] **Step 1: Write deterministic identifier tests**

Add tests using this exact public contract:

```c
#define DF_RUNTIME_ID_HEX_LENGTH 16U
typedef int (*df_runtime_id_fill_fn)(uint8_t *, size_t, void *);
int df_runtime_id_generate(char output[DF_RUNTIME_ID_HEX_LENGTH + 1U],
    df_runtime_id_fill_fn fill, void *context);
bool df_runtime_id_is_valid(const char *value);
```

Feed bytes `{0x00,0x12,0xab,0xff,0x80,0x7e,0x55,0x09}` and assert the exact output `0012abff807e5509`. Assert uppercase, short, long, and non-hex strings are invalid; assert a failing or short random read clears the output and returns `DF_ERR_IO`.

- [ ] **Step 2: Run the focused suite and confirm the new symbols are absent**

Run: `make clean && make test`

Expected: compilation fails because `runtime_id.h` or `df_runtime_id_generate` does not exist.

- [ ] **Step 3: Implement runtime ID generation and daemon ownership**

Implement a complete-read loop for `/dev/urandom`, lowercase hex encoding, and exact validation. Add this field to `struct df_runtime_ubus`:

```c
char runtime_id[DF_RUNTIME_ID_HEX_LENGTH + 1U];
```

Generate it once in `df_runtime_ubus_start()`, preserve it across ubus reconnects, clear it in `df_runtime_ubus_stop()`, and emit it beside `running` and `mode`:

```c
blobmsg_add_string(&platform->response, "runtime_id",
                   platform->owner->runtime_id);
```

Extend the ubus lifecycle test to assert the stored value passes `df_runtime_id_is_valid()` while started and is empty after stop.

- [ ] **Step 4: Run identifier and full C tests**

Run: `make clean && make test`

Expected: all C tests pass and the printed suite count matches the updated `test_suite_count()`.

- [ ] **Step 5: Commit the independently reviewable runtime identity**

```bash
git add Makefile src/runtime_id.h src/runtime_id.c src/runtime_ubus.h src/runtime_ubus.c tests/test_runtime_id.c tests/test_runtime_ubus.c tests/test_main.c
git commit -m "Add daemon runtime identity"
```

### Task 2: Producer lease and secure state file

**Files:**
- Create: `src/pcm_http.h`
- Create: `src/pcm_http.c`
- Create: `tests/test_pcm_http.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: the current runtime ID, call state, call generation, and audio-transmit status through `df_pcm_http_ops.read_status`.
- Produces: `df_pcm_http_handle()` for all three operations and a structured response consumed by the CGI renderer in Task 4.

- [ ] **Step 1: Define the core interface and failing session tests**

Use these names and types so later tasks can compile independently:

```c
#define DF_PCM_HTTP_FRAME_BYTES 320U
#define DF_PCM_HTTP_MAX_FRAMES 5U
#define DF_PCM_HTTP_LEASE_MS 2000U

enum df_pcm_http_operation {
    DF_PCM_HTTP_SESSION_OPEN,
    DF_PCM_HTTP_SUBMIT,
    DF_PCM_HTTP_SESSION_END
};

struct df_pcm_http_status {
    char runtime_id[17];
    char call_state[16];
    uint64_t generation;
    bool audio_tx_active;
    uint64_t audio_tx_generation;
};

struct df_pcm_http_request {
    enum df_pcm_http_operation operation;
    const char *method;
    const char *runtime_id;
    const char *generation;
    const char *sequence;
    const char *session_token;
    const char *content_type;
    const char *content_length;
    int body_fd;
};

struct df_pcm_http_response {
    unsigned http_status;
    char error[32];
    char runtime_id[17];
    char audio_session[33];
    uint64_t generation;
    uint64_t accepted_frames;
    uint64_t next_sequence;
    uint64_t lease_ms;
};

struct df_pcm_http_ops {
    int (*read_status)(struct df_pcm_http_status *, void *);
    int (*fill_random)(uint8_t *, size_t, void *);
    uint64_t (*now_ms)(void *);
    int (*sleep_until_ms)(uint64_t, void *);
    int (*send_pcm)(const char *, uint64_t, const int16_t *, size_t, void *);
    void *context;
};

struct df_pcm_http_config {
    const char *state_path;
    const char *socket_path;
    struct df_pcm_http_ops ops;
};

int df_pcm_http_handle(const struct df_pcm_http_config *,
    const struct df_pcm_http_request *, struct df_pcm_http_response *);
```

Test exact `POST` enforcement, runtime/generation validation, talking plus matching active audio-transmit requirements, deterministic 128-bit token encoding, open response fields, active-producer `409 producer_busy`, same-token release, wrong-token release, and lease takeover at 2,000 ms with preserved `next_sequence`.

- [ ] **Step 2: Run the focused suite and observe the missing core**

Run: `make clean && make test`

Expected: compilation fails on missing `pcm_http.h` or `df_pcm_http_handle`.

- [ ] **Step 3: Implement strict parsing and locked state transitions**

Parse unsigned decimal with `strtoull` plus full-string and overflow checks. Open the state path with `O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW`, request mode `0600`, verify with `fstat()` that it is regular, owned by `geteuid()`, and exactly `0600`, then take `flock(fd, LOCK_EX)` before reading status or state.

Persist one newline-terminated record with exact keys and decimal values. A new runtime or generation starts at sequence zero; a lease takeover within the same runtime and generation preserves the stored sequence:

```text
runtime_id=0123456789abcdef
generation=42
next_sequence=100
audio_session=0123456789abcdef0123456789abcdef
lease_deadline_ms=123456
```

Reject duplicate, missing, unknown, oversized, or malformed fields. Only session-open may replace runtime/generation state, and it must re-read authoritative status after taking the lock. Session-end clears the token and deadline while preserving runtime, generation, and next sequence.

- [ ] **Step 4: Add adversarial state-file tests**

Assert rejection of a symlink, directory, FIFO, wrong mode, malformed record, second live token, old token after expiry, and an old session-open request blocked on the lock while the fake authoritative runtime changes. The suspended old request must return `409` and must not rewrite the newer state.

- [ ] **Step 5: Run the C suite under normal and sanitizer builds**

Run:

```bash
make clean && make test
make clean && make CC=clang CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

Expected: both runs pass without sanitizer diagnostics.

- [ ] **Step 6: Commit session ownership and state safety**

```bash
git add Makefile src/pcm_http.h src/pcm_http.c tests/test_pcm_http.c tests/test_main.c
git commit -m "Add bounded PCM producer sessions"
```

### Task 3: PCM batches, recovery, and pacing

**Files:**
- Modify: `src/pcm_http.c`
- Modify: `tests/test_pcm_http.c`

**Interfaces:**
- Consumes: the session and state-file contract from Task 2 and `send_pcm(path, generation, pcm, 160, context)`.
- Produces: complete `DF_PCM_HTTP_SUBMIT` handling with `accepted_frames` and `next_sequence` recovery metadata.

- [ ] **Step 1: Add failing request-shape and body tests**

Cover missing or non-octet-stream content type, lengths other than the five allowed values, zero generation, malformed or overflowing sequence, `sequence + frame_count` overflow, short body, trailing byte, expired/wrong token, stale runtime/generation, duplicate sequence, and a gap above the expected sequence. A short or trailing body must produce zero `send_pcm` calls.

- [ ] **Step 2: Add failing pacing and partial-send tests**

Feed five frames with unique little-endian sample patterns. Start the fake monotonic clock at 1,000 ms and assert send deadlines are exactly `1000`, `1020`, `1040`, `1060`, and `1080`. Make the third send fail and assert HTTP `503`, `accepted_frames == 2`, and `next_sequence == initial_sequence + 2`; retry begins at that returned sequence and never resends the first two frames.

- [ ] **Step 3: Run the focused suite and confirm submit remains unsupported**

Run: `make clean && make test`

Expected: the new submit assertions fail while session open/end tests remain green.

- [ ] **Step 4: Implement full-body admission and little-endian conversion**

Read exactly `Content-Length + 1` bytes into a fixed `DF_PCM_HTTP_FRAME_BYTES * DF_PCM_HTTP_MAX_FRAMES + 1` stack buffer, retry `EINTR`, reject EOF before the declared length, and reject a trailing byte before any send. Convert each pair explicitly:

```c
uint16_t raw = (uint16_t)body[offset] |
               ((uint16_t)body[offset + 1U] << 8U);
pcm[sample] = raw <= INT16_MAX
    ? (int16_t)raw
    : (int16_t)(-(int32_t)(UINT16_MAX - raw + 1U));
```

- [ ] **Step 5: Implement sequence admission, absolute pacing, and prefix accounting**

Under the same state lock, re-read authoritative status and require exact runtime/generation, `talking`, and matching active audio transmission. Require the live token and exact next sequence. Send the first frame immediately; before later frames call `sleep_until_ms(first_deadline + index * 20)`. After each successful datagram, increment and persist `next_sequence`, `accepted_frames`, and the renewed lease deadline. Never retry a successful datagram and never synthesize a missing frame.

- [ ] **Step 6: Run normal and sanitizer suites**

Run the two commands from Task 2 Step 5.

Expected: all validation, recovery, concurrency, PCM-preservation, and pacing tests pass without sanitizer diagnostics.

- [ ] **Step 7: Commit the independently testable batch engine**

```bash
git add src/pcm_http.c tests/test_pcm_http.c
git commit -m "Submit paced HTTP PCM batches"
```

### Task 4: Production CGI, ubus adapter, and package lifecycle

**Files:**
- Create: `src/pcm_http_ubus.h`
- Create: `src/pcm_http_ubus.c`
- Create: `src/pcm_http_main.c`
- Create: `tests/test_pcm_http_cli.sh`
- Modify: `package/doorfast/files/doorfast-http.sh`
- Modify: `tests/test_doorfast_http.sh`
- Modify: `package/doorfast/files/doorfast.init`
- Modify: `tests/test_doorfast_init.sh`
- Modify: `package/doorfast/Makefile`
- Modify: `tests/test_package_manifest.sh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `df_pcm_http_handle()`, `ubus call doorfast status` fields `runtime_id`, `call.session`, `call.generation`, `audio_tx.active`, and `audio_tx.generation`, plus `df_gvs_pcm_ingress_send()`.
- Produces: installed `/usr/sbin/doorfast-pcm-http` and three CGI routes under `/cgi-bin/doorfast/api/v1/audio`.

- [ ] **Step 1: Write failing CGI process tests**

Build `doorfast-pcm-http` for host tests with a compile-time fake adapter. Invoke it with CGI environment variables and assert exact status lines and JSON for malformed method/query/header/length cases. Verify a binary body containing NUL bytes reaches the fake send callback unchanged and never appears in stderr. Assert output always includes `Content-Type: application/json` and `Cache-Control: no-store`.

- [ ] **Step 2: Write failing shell-dispatch and lifecycle tests**

Extend `tests/test_doorfast_http.sh` to create a temporary test copy of the CGI script with its fixed `/usr/sbin/doorfast-pcm-http` assignment replaced by a fake helper path. Assert the three exact audio paths use `exec` before existing status/snapshot logic and preserve stdin. Extend init tests to assert `rm -f /tmp/doorfast-pcm-http.state` occurs only when an enabled service starts, before `procd_open_instance`.

- [ ] **Step 3: Implement the production ubus status adapter**

Connect to ubus, look up `doorfast`, invoke `status`, and parse only the five required fields. Reject missing, wrong-type, oversized, uppercase runtime IDs, zero generations, or mismatched audio-transmit generation. Do not call a shell or accept an object/path override in the installed build.

- [ ] **Step 4: Implement CGI parsing and response rendering**

Map only these paths:

```text
/api/v1/audio/session       -> DF_PCM_HTTP_SESSION_OPEN
/api/v1/audio/submit.pcm    -> DF_PCM_HTTP_SUBMIT
/api/v1/audio/session/end   -> DF_PCM_HTTP_SESSION_END
```

Reject duplicate and unknown query keys. Read `REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_TYPE`, `CONTENT_LENGTH`, and `HTTP_X_DOORFAST_AUDIO_SESSION`; keep stdin as `body_fd`. Render JSON from fixed fields with HTTP `200`, `400`, `409`, or `503`, and never echo raw request text.

- [ ] **Step 5: Build and install the helper in the APK**

Add an explicit target compile using:

```make
$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -D_DEFAULT_SOURCE \
  -DDF_WITH_UBUS -DDF_PCM_HTTP_PROGRAM -std=c17 -Wall -Wextra -Werror -pedantic \
  -I$(PKG_BUILD_DIR)/src \
  $(PKG_BUILD_DIR)/src/pcm_http.c \
  $(PKG_BUILD_DIR)/src/pcm_http_ubus.c \
  $(PKG_BUILD_DIR)/src/pcm_http_main.c \
  $(PKG_BUILD_DIR)/src/gvs_pcm_ingress.c \
  -lubus -lubox -lblobmsg_json -o $(PKG_BUILD_DIR)/doorfast-pcm-http
```

Install it as `/usr/sbin/doorfast-pcm-http`, bump `PKG_RELEASE` from `38` to `39`, and extend package-list assertions for the helper. The installed shell route always uses that fixed absolute path; the test suite modifies only its temporary script copy.

- [ ] **Step 6: Run all host checks used by Actions**

Run:

```bash
make clean && make test
sh tests/test_pcm_http_cli.sh
sh tests/test_doorfast_http.sh
sh tests/test_doorfast_init.sh
sh tests/test_package_manifest.sh
```

Expected: every command exits zero; the tests prove binary stdin bypasses shell variables and state cleanup precedes daemon launch.

- [ ] **Step 7: Commit production wiring as one package change**

```bash
git add Makefile src/pcm_http_ubus.h src/pcm_http_ubus.c src/pcm_http_main.c package/doorfast/files/doorfast-http.sh package/doorfast/files/doorfast.init package/doorfast/Makefile tests/test_pcm_http_cli.sh tests/test_doorfast_http.sh tests/test_doorfast_init.sh tests/test_package_manifest.sh
git commit -m "Package the HTTP PCM bridge"
```

### Task 5: Non-installed acceptance binary and VM runner

**Files:**
- Modify: `src/pcm_http_main.c`
- Create: `tests/run_doorfast_vm_pcm_http.py`
- Modify: `package/doorfast/Makefile`
- Modify: `.github/workflows/build-apk.yml`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: the same `pcm_http.c` core as production.
- Produces: Actions artifact `doorfast-pcm-http-acceptance` and a VM runner that never modifies production network UCI or firewall state.

- [ ] **Step 1: Add failing manifest/workflow checks**

Assert the package compiles `doorfast-pcm-http-acceptance` with `-DDF_PCM_HTTP_ACCEPTANCE`, does not install that filename in the APK, copies it to an Actions staging directory, and uploads it beside both APKs. Assert the installed package list contains only `doorfast-pcm-http`.

- [ ] **Step 2: Implement the acceptance-only provider**

Under `#ifdef DF_PCM_HTTP_ACCEPTANCE`, replace the ubus adapter with a provider that re-reads a root-owned test status file on every status call and accepts socket/state paths only from required command-line flags:

```text
doorfast-pcm-http-acceptance --status-file FILE --state FILE --socket FILE
```

The status file has one strict record:

```text
runtime_id=0123456789abcdef
call_state=talking
generation=42
audio_tx_active=1
audio_tx_generation=42
```

Reject extra keys, duplicate keys, loose permissions, and non-regular files. Compile this binary with the ImmortalWrt target compiler and do not install it.

- [ ] **Step 3: Implement installed-path VM rejection checks**

In `tests/run_doorfast_vm_pcm_http.py`, snapshot `uci export network`, `uci export firewall`, service PIDs, and ubus status. Exercise the installed CGI while the real daemon is idle and assert valid-shape session open returns `409`; also assert malformed runtime, stale generation, wrong method, short body, and 1,920-byte body are rejected without a datagram or state transition.

- [ ] **Step 4: Implement isolated successful-path VM checks**

Copy the acceptance binary to `/tmp`, create mode-`0600` status/state fixtures, and bind an isolated Unix datagram receiver. Assert session creation, lease renewal, producer exclusion, release, expiry takeover, ordered five-frame delivery, exact runtime/generation framing, and delivery intervals no shorter than 18 ms with a documented VM tolerance. Unlink the socket after two receives to assert `503`, `accepted_frames: 2`, and the matching `next_sequence`.

- [ ] **Step 5: Implement stale-request and no-replay VM checks**

Hold the state-file lock from the Python runner, launch a session-open process for runtime A, rewrite authoritative status to runtime B/generation 43, then release the lock. Assert the old process returns `409` and state remains on runtime B after a new valid session. Restart the producer process with the current token and old sequence; assert sequence mismatch returns the advanced `next_sequence` and sends no datagram.

- [ ] **Step 6: Verify cleanup and platform invariants**

Remove the acceptance binary, status file, state file, socket, and captured PCM. Compare network/firewall snapshots byte-for-byte, confirm the original Doorfast PID/service state is restored, and confirm the installed CGI still rejects an idle session.

- [ ] **Step 7: Run host structural checks**

Run:

```bash
python3 -m py_compile tests/run_doorfast_vm_pcm_http.py
sh tests/test_package_manifest.sh
make clean && make test
```

Expected: syntax, package-boundary, and C tests pass. The real VM execution waits for the Actions artifact and is not simulated by this step.

- [ ] **Step 8: Commit VM acceptance support**

```bash
git add src/pcm_http_main.c package/doorfast/Makefile .github/workflows/build-apk.yml tests/test_package_manifest.sh tests/run_doorfast_vm_pcm_http.py
git commit -m "Add VM acceptance for HTTP PCM"
```

### Task 6: Contract documentation and complete verification

**Files:**
- Modify: `docs/doorfast-http-bridge.md`
- Modify: `docs/local-pcm-ingress.md`
- Modify: `docs/current-capability-status.md`

**Interfaces:**
- Consumes: the final response codes, JSON fields, package release, host-test output, Actions artifact metadata, and VM acceptance evidence from Tasks 1–5.
- Produces: an operator/client contract that separates local ingress acceptance from physical playback evidence.

- [ ] **Step 1: Document the exact client state machine**

Add the three routes, required headers/query fields, five legal body sizes, one-in-flight rule, 2-second lease, status/error JSON, duplicate/gap recovery, and the rule to pause capture and discard newly recorded audio after failure. State that runtime or generation change ends capture and discards buffered frames.

- [ ] **Step 2: Document the trust and evidence boundary**

Explain that the token coordinates one producer, the CGI still needs trusted-network or HTTPS protection, HTTP success means local Unix ingress acceptance, and actual MT8157 speaker format, intelligibility, latency, echo, and long-call stability remain physical-device acceptance items.

- [ ] **Step 3: Run the full repository verification once**

Run every `host-tests` command from `.github/workflows/build-apk.yml` in its listed order. Then run `git diff --check` and search changed files for unresolved placeholder markers.

Expected: every command exits zero, `git diff --check` is silent, and the placeholder search returns no matches.

- [ ] **Step 4: Record only measured Actions and VM evidence**

After Actions supplies the r39 APK and acceptance binary, run:

```bash
python3 -B tests/run_doorfast_vm_pcm_http.py /absolute/path/to/vm/ssh.sh /absolute/path/to/doorfast-0.1.0-r39.apk /absolute/path/to/doorfast-pcm-http-acceptance
```

Update `docs/current-capability-status.md` with the actual Actions run, artifact hashes, VM version, observed pacing range, and cleanup result. If the VM run has not occurred, label the bridge as host-tested and leave VM acceptance explicitly pending.

- [ ] **Step 5: Commit the verified public contract**

```bash
git add docs/doorfast-http-bridge.md docs/local-pcm-ingress.md docs/current-capability-status.md
git commit -m "Document HTTP PCM producer contract"
```

## Execution sequence

Implement each task on its own `codex/` branch and open an English-titled PR after its focused verification passes. Merge in order because Tasks 2–6 consume interfaces from earlier tasks. Do not poll GitHub Actions; wait for the user to report completion or failure, inspect a reported failure once, and then continue from the exact failing job evidence.
