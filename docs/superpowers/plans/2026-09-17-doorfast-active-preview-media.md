# Doorfast Active Preview Media Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let an active-host Doorfast instance request one evidenced GVS door-station preview, encode admitted JPEG frames as H.264, and publish the result to a pre-created Home Assistant go2rtc RTSP ingress.

**Architecture:** The existing daemon remains the sole GVS network endpoint. It loads a fixed-ABI media shared object only when `media_enabled=1`; the optional `doorfast-media` APK owns monitor state, a bounded JPEG queue, the FFmpeg child, RTSP publishing, relay delivery, and credential loading. The base daemon validates config, supplies exact control-frame transmission and observed routes to the module, delivers already-validated JPEGs before its snapshot cache, and exposes media operations through the existing ubus/CGI bridge.

**Tech Stack:** C17, libubus/blobmsg, libuci, dlopen, POSIX pipes/process control, FFmpeg, RTSP/TCP, ImmortalWrt 25.12.1 APK packages, LuCI JavaScript, pytest/unittest VM fixtures.

**Spec:** `docs/superpowers/specs/2026-09-17-doorfast-ha-webrtc-media-design.md`

## Global Constraints

- Default is disabled: `media_enabled=0`; passive mode and an absent media APK never emit a GVS preview request.
- Preview has one protocol-verified GVS slot. A higher user capacity is reported as unavailable until multi-station GVS routing is separately evidenced.
- A station address is an exact six-byte `32:bb:uu:00:gg:00` hexadecimal value from capture or the property device table. Do not derive it from the indoor `IS:` address.
- A preview send requires that exact station address plus a fresh observed `07/86` route, or an explicit IPv4 fallback. It must not broadcast or guess a route.
- The only preview wire exchange is `03/04` body `02 20 6f 00 20 6e 1e`, matching `03/84`, and `03/02` on stop. Preview never sends `03/03`, `03/55`, audio, unlock, or elevator commands.
- Every monitor action is bound to an increasing local `generation`; stale control or JPEG input cannot mutate a later generation.
- FFmpeg receives frames through a bounded in-memory queue and a root-only pipe. It is invoked with `fork`/`execvp`, never a shell command or `/tmp/doorfast-latest.jpg` polling.
- H.264 is yuv420p, Baseline by default, no B-frames, 5/8/10/12/15 fps, one-second GOP, repeated SPS/PPS, RTP packetization mode 1, and RTSP over TCP.
- Doorfast connects only to the configured HA address and `8554/TCP`. It never accesses go2rtc `1984`, starts a RTSP/WebRTC listener, or exposes media services to the building interface.
- Store RTSP and HA relay credentials only in `/etc/doorfast/media-credentials`, mode `0600`; do not return them in ubus, CGI, LuCI, logs, test traces, diagnostics, or exported artifacts.
- `doorfast-media` depends on `ffmpeg` and `libffmpeg-full`. The selected SDK build must include `libx264` before software encoder acceptance. VAAPI/QSV are optional runtime choices, not unconditional APK dependencies.
- Incoming calls preempt preview. Cleanup closes the pipe, reaps the child, sends RTSP TEARDOWN through FFmpeg shutdown, invalidates the active generation, and leaves the go2rtc empty stream definition in place.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/gvs_station.h`, `src/gvs_station.c` | Strict door-station address parsing and a time-bounded observed-route table. |
| `src/gvs_monitor.h`, `src/gvs_monitor.c` | Pure `03/04`/`03/84`/`03/02` monitor state machine and generation admission. |
| `src/media_frame_queue.h`, `src/media_frame_queue.c` | Fixed-capacity copied JPEG queue that drops the oldest unencoded frame. |
| `src/media_capacity.h`, `src/media_capacity.c` | User/resource/protocol capacity calculation and encoder availability selection. |
| `src/media_encoder.h`, `src/media_encoder.c` | FFmpeg argv construction, pipe ownership, process reaping, resolution restart, and redacted status. |
| `src/media_credentials.h`, `src/media_credentials.c` | Atomic root-only credentials read/write and no-secret status. |
| `src/media_relay.h`, `src/media_relay.c` | Bounded asynchronous HA event relay using a configured `http` or `https` URL and bearer token. |
| `src/media_module.h`, `src/media_module.c` | `doorfast-media` module ABI implementation that combines monitor, queue, encoder, relay, and status. |
| `src/runtime_media_module.h`, `src/runtime_media_module.c` | Base-daemon fixed-path module loader and safe callback boundary. |
| `src/runtime_config.[ch]`, `src/config.[ch]` | Strict UCI parsing/validation for all public media settings, with no secret in UCI. |
| `src/gvs_udp_sender.[ch]` | Exact raw GVS control emission and trusted observed-route access for the module callbacks. |
| `src/runtime_service.c` | Module lifecycle, preemption ordering, validated JPEG delivery, and control-frame delivery. |
| `src/runtime_ubus.[ch]` | Monitor commands, credentials action, and a redacted `media` status table. |
| `package/doorfast/files/doorfast-http.sh` | `monitor` CGI endpoints that proxy only fixed ubus methods. |
| `package/doorfast-media/Makefile` | Optional module/FFmpeg APK with an ABI-coupled version and no listener service. |
| `package/doorfast/files/doorfast.config` | Disabled media defaults, never credential values. |
| `package/luci-app-doorfast/.../status.js` | Media configuration form, secret set/clear action, and runtime status. |
| `tests/test_gvs_station.c`, `tests/test_gvs_monitor.c`, `tests/test_media_*.c` | Unit coverage for all media state and OS-facing adapters. |
| `tests/test_doorfast_http.sh`, `tests/run_doorfast_vm_media.py` | CGI contract and installed-APK VM acceptance. |

### Task 1: Strict Door-Station Identity and Route Evidence

**Files:**
- Create: `src/gvs_station.h`
- Create: `src/gvs_station.c`
- Create: `tests/test_gvs_station.c`
- Modify: `tests/Makefile`

**Interfaces:**
- Produces `int df_gvs_station_parse(const char *text, uint8_t output[6]);`.
- Produces `int df_gvs_station_routes_observe(struct df_gvs_station_routes *, const uint8_t peer[6], uint32_t ipv4, uint64_t now_ms, bool discovery_reply);`.
- Produces `int df_gvs_station_routes_lookup(const struct df_gvs_station_routes *, const uint8_t peer[6], uint64_t now_ms, uint64_t max_age_ms, uint32_t *ipv4);`.

- [x] **Step 1: Write the failing parsing and freshness tests**

```c
void test_gvs_station_parses_only_captured_door_station_shape(void) {
    uint8_t station[6];
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_parse("32:02:01:00:02:00", station));
    TEST_ASSERT_MEM_EQ((uint8_t[]){0x32, 2, 1, 0, 2, 0}, station, 6);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_parse("IS:2-1-101-1", station));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_parse("32:02:01:01:02:00", station));
}

void test_gvs_station_route_requires_matching_fresh_discovery_reply(void) {
    struct df_gvs_station_routes routes = {0};
    const uint8_t station[6] = {0x32, 2, 1, 0, 2, 0};
    uint32_t ipv4 = 0;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_routes_lookup(&routes, station, 100, 60000, &ipv4));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_routes_observe(&routes, station, 0x01020304, 100, true));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_routes_lookup(&routes, station, 60099, 60000, &ipv4));
    TEST_ASSERT_INT_EQ(0x01020304, (int)ipv4);
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_station_routes_lookup(&routes, station, 60100, 60000, &ipv4));
}
```

- [x] **Step 2: Run the test to verify it fails**

Run: `make -B build/doorfast-tests`

Expected: compile failure because `gvs_station.h` does not exist.

- [x] **Step 3: Implement exact address and route boundaries**

```c
#define DF_GVS_STATION_ROUTE_CAPACITY 4U
struct df_gvs_station_route { uint8_t peer[6]; uint32_t ipv4; uint64_t discovery_ms; bool valid; };
struct df_gvs_station_routes { struct df_gvs_station_route entries[DF_GVS_STATION_ROUTE_CAPACITY]; size_t next; };

int df_gvs_station_parse(const char *text, uint8_t out[6]) {
    unsigned b[6]; char tail;
    if (text == NULL || out == NULL ||
        sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%c", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &tail) != 6 ||
        b[0] != 0x32U || b[3] != 0U || b[5] != 0U || b[1] > 0x99U || b[2] > 0x09U || b[4] == 0U || b[4] > 0x99U) return DF_ERR_INVALID;
    for (size_t i = 0; i < 6; ++i) out[i] = (uint8_t)b[i];
    return DF_OK;
}
```

Require `discovery_reply=true`, the exact station bytes, nonzero IPv4, monotonic time, and `now_ms - discovery_ms < max_age_ms`; do not silently refresh a discovery lease from unrelated traffic.

- [x] **Step 4: Run focused and full native tests**

Run: `make -B test`

Expected: `test_gvs_station_*` passes and existing tests remain green.

- [x] **Step 5: Commit the unit**

```bash
git add src/gvs_station.[ch] tests/test_gvs_station.c tests/Makefile
git commit -m "feat: validate door station routes for preview"
```

### Task 2: Pure Monitor Protocol State Machine

**Files:**
- Create: `src/gvs_monitor.h`
- Create: `src/gvs_monitor.c`
- Create: `tests/test_gvs_monitor.c`
- Modify: `tests/Makefile`

**Interfaces:**
- Consumes `df_gvs_station_parse` output and parsed `struct df_gvs_frame`.
- Produces `int df_gvs_monitor_start(struct df_gvs_monitor *, const uint8_t local[6], const uint8_t station[6], uint64_t now_ms);`.
- Produces `int df_gvs_monitor_step(struct df_gvs_monitor *, uint64_t now_ms, struct df_gvs_monitor_action *);`.
- Produces `int df_gvs_monitor_receive(struct df_gvs_monitor *, const struct df_gvs_frame *, uint32_t source_ipv4, uint64_t now_ms, struct df_gvs_monitor_result *);`.
- Produces `int df_gvs_monitor_admit_jpeg(const struct df_gvs_monitor *, const uint8_t source[6], const uint8_t destination[6], uint32_t source_ipv4, uint64_t generation);`.

- [x] **Step 1: Write failing wire-state tests from the captured sequence**

```c
void test_monitor_retries_0304_then_accepts_only_matching_0384(void) {
    struct df_gvs_monitor monitor = {0}; struct df_gvs_monitor_action action;
    const uint8_t local[6] = {0x61,2,1,1,1,1};
    const uint8_t station[6] = {0x32,2,1,0,2,0};
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_start(&monitor, local, station, 100));
    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_monitor_step(&monitor, 100, &action));
    TEST_ASSERT_MEM_EQ((uint8_t[]){2,0x20,0x6f,0,0x20,0x6e,0x1e}, action.payload, 7);
    TEST_ASSERT_INT_EQ(0x04, action.opcode);
    TEST_ASSERT_INT_EQ(DF_OK, receive_frame(&monitor, station, local, 0x84, (uint8_t[]){0x1e,0,1}, 3, 0x01020304, 150));
    TEST_ASSERT_INT_EQ(DF_GVS_MONITOR_AWAITING_VIDEO, monitor.state);
}

void test_monitor_rejects_stale_generation_wrong_route_and_unconfirmed_reply(void) {
    /* 03/50, wrong source, wrong destination, stale source IPv4, and old generation remain non-mutating. */
}
```

- [x] **Step 2: Run the test to verify it fails**

Run: `make -B build/doorfast-tests`

Expected: compile failure because the monitor interface is absent.

- [x] **Step 3: Implement bounded state transitions**

```c
enum df_gvs_monitor_state { DF_GVS_MONITOR_IDLE, DF_GVS_MONITOR_REQUESTING,
    DF_GVS_MONITOR_AWAITING_VIDEO, DF_GVS_MONITOR_PUBLISHING,
    DF_GVS_MONITOR_VIEWING, DF_GVS_MONITOR_STOPPING, DF_GVS_MONITOR_FAILED };

struct df_gvs_monitor_action { bool send; uint8_t family, opcode, destination[6], source[6], payload[7]; size_t payload_length; uint64_t generation; };
```

Increment the generation at a successful start, send at most three `03/04` requests one second apart, require exact source/destination, source IPv4, `03/84`, and the captured body `1e 00 01`. Timeout to `FAILED` without creating an encoder. Stop sends one `03/02` body `00`, waits at most one second for matching `03/82`, then enters `IDLE` locally even if remote confirmation is absent. `03/50` sets `monitor_unconfirmed`, never success.

- [x] **Step 4: Run tests**

Run: `make -B test`

Expected: monitor retries, timeout, confirmation, stop timeout, and generation isolation all pass.

- [x] **Step 5: Commit the unit**

```bash
git add src/gvs_monitor.[ch] tests/test_gvs_monitor.c tests/Makefile
git commit -m "feat: add GVS active preview state machine"
```

### Task 3: Strict Media Configuration and Credentials

**Files:**
- Modify: `src/config.h`
- Modify: `src/config.c`
- Modify: `src/runtime_config.h`
- Modify: `src/runtime_config.c`
- Create: `src/media_credentials.h`
- Create: `src/media_credentials.c`
- Modify: `tests/test_config.c`
- Modify: `tests/test_runtime_config.c`
- Create: `tests/test_media_credentials.c`
- Modify: `package/doorfast/files/doorfast.config`

**Interfaces:**
- Produces `struct df_media_config` embedded in `struct df_config`.
- Produces `int df_media_credentials_write(const char *path, const struct df_media_credentials *replacement, const struct df_media_credentials_update *update);`.
- Produces `int df_media_credentials_load(const char *path, struct df_media_credentials *out);`.

- [x] **Step 1: Write failing validation and secret-durability tests**

```c
void test_runtime_config_requires_valid_media_prerequisites(void) {
    const char bad[] = "config gvs 'main'\n option enabled '1'\n option active_host '1'\n option passive_only '0'\n option gvs_interface 'eth2'\n option gvs_local_address 'IS:2-1-101-1'\n option media_enabled '1'\n";
    struct df_runtime_config runtime;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_config_parse(bad, &runtime));
}

void test_media_credentials_preserve_blank_fields_and_never_echo_values(void) {
    struct df_media_credentials initial = {.rtsp_password = "secret-a", .relay_token = "secret-b"};
    struct df_media_credentials_update update = {.set_rtsp_password = true, .rtsp_password = "secret-c"};
    /* write, reload, assert relay token remains; status exposes only booleans */
}
```

- [x] **Step 2: Run tests to verify they fail**

Run: `make -B build/doorfast-tests`

Expected: missing `df_media_config` and credentials symbols.

- [x] **Step 3: Implement all public media fields and no secret UCI fields**

```c
struct df_media_config {
    bool enabled; char station_address[18], station_ipv4[16], go2rtc_host[64], stream_name[65], rtsp_username[33];
    uint16_t go2rtc_port; enum df_media_encoder encoder; enum df_media_resolution resolution;
    uint8_t fps, max_encoders, publish_retries; uint16_t bitrate_kbps, min_free_kib, preview_timeout_s, first_frame_timeout_s;
    enum df_media_profile profile; enum df_media_overload_policy overload_policy; bool diagnostics;
    char relay_url[256];
};
```

Accept only the documented enum values, host/IP without path/userinfo, port `1..65535`, stream `^[A-Za-z0-9_-]{1,64}$`, FPS in `{5,8,10,12,15}`, bitrate `256..2000`, and timeout/range limits from the spec. When enabled require active host, a valid `32:...` station address, go2rtc host, and credentials file path exactly `/etc/doorfast/media-credentials`. UCI contains `media_rtsp_username`, never a password/token. Credential files are atomically renamed from a `0600` sibling temporary file and accept bounded printable ASCII excluding newline, carriage return, and NUL.

- [x] **Step 4: Run configuration and secret tests**

Run: `make -B test`

Expected: invalid startup config fails closed; credentials preserve an omitted value, clear only on explicit flag, and status has only `rtsp_password_set`/`relay_token_set`.

- [x] **Step 5: Commit the unit**

```bash
git add src/config.[ch] src/runtime_config.[ch] src/media_credentials.[ch] tests/test_config.c tests/test_runtime_config.c tests/test_media_credentials.c package/doorfast/files/doorfast.config
git commit -m "feat: add validated media configuration and credentials"
```

### Task 4: Bounded Frame Queue and Capacity Gate

**Files:**
- Create: `src/media_frame_queue.h`
- Create: `src/media_frame_queue.c`
- Create: `src/media_capacity.h`
- Create: `src/media_capacity.c`
- Create: `tests/test_media_frame_queue.c`
- Create: `tests/test_media_capacity.c`
- Modify: `tests/Makefile`

**Interfaces:**
- Produces `int df_media_frame_queue_push(struct df_media_frame_queue *, const uint8_t *jpeg, size_t length, uint64_t generation, uint64_t timestamp_ms);` and `int df_media_frame_queue_pop(...)`.
- Produces `unsigned df_media_effective_capacity(unsigned requested, unsigned resource_limit, unsigned protocol_limit);`.
- Produces `int df_media_encoder_select(enum df_media_encoder requested, const struct df_media_encoder_probe *, enum df_media_encoder *selected);`.

- [ ] **Step 1: Write failing queue and capacity tests**

```c
void test_media_queue_discards_oldest_and_never_aliases_reassembly_memory(void) {
    struct df_media_frame_queue queue; uint8_t first[] = {1}, second[] = {2}, third[] = {3};
    df_media_frame_queue_init(&queue, 2, 16);
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(&queue, first, 1, 7, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(&queue, second, 1, 7, 11));
    TEST_ASSERT_INT_EQ(DF_OK, df_media_frame_queue_push(&queue, third, 1, 7, 12));
    first[0] = 9; TEST_ASSERT_INT_EQ(1, queue.dropped_oldest);
    TEST_ASSERT_MEM_EQ(second, queue.entries[queue.read].data, 1);
}

void test_media_capacity_cannot_exceed_verified_single_station_limit(void) {
    TEST_ASSERT_INT_EQ(1, df_media_effective_capacity(4, 4, 1));
    TEST_ASSERT_INT_EQ(0, df_media_effective_capacity(1, 0, 1));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -B build/doorfast-tests`

Expected: queue and capacity symbols are absent.

- [ ] **Step 3: Implement fixed allocations and fail-closed capacity**

Use four queue entries of at most `DF_GVS_VIDEO_MAX_FRAME`, allocate each once at initialization, copy bytes on enqueue, reject a generation mismatch, and free every entry at destroy. `auto` maps to one user slot; `resource_limit` is zero when free memory is below `media_min_free_kib` or encoder probing fails; `protocol_limit` is `1` in this release. An explicitly requested unavailable encoder returns `DF_ERR_INVALID`; `auto` tries QSV, VAAPI, then software.

- [ ] **Step 4: Run tests**

Run: `make -B test`

Expected: no queue operation reallocates on packet arrival; overflow and unavailable encoders are deterministic.

- [ ] **Step 5: Commit the unit**

```bash
git add src/media_frame_queue.[ch] src/media_capacity.[ch] tests/test_media_frame_queue.c tests/test_media_capacity.c tests/Makefile
git commit -m "feat: bound preview frame buffering and capacity"
```

### Task 5: FFmpeg and RTSP Publisher Supervisor

**Files:**
- Create: `src/media_encoder.h`
- Create: `src/media_encoder.c`
- Create: `tests/test_media_encoder.c`
- Modify: `tests/Makefile`

**Interfaces:**
- Consumes one `df_media_frame_queue` and loaded credentials.
- Produces `int df_media_encoder_start(struct df_media_encoder_process *, const struct df_media_encoder_config *, const struct df_media_credentials *, uint64_t generation);`.
- Produces `int df_media_encoder_write_frame(struct df_media_encoder_process *, const struct df_media_frame *);`.
- Produces `int df_media_encoder_tick(struct df_media_encoder_process *, uint64_t now_ms);`.
- Produces `int df_media_encoder_stop(struct df_media_encoder_process *, unsigned timeout_ms);` so runtime integration can observe reaping failures and bounded timeout expiry.

- [ ] **Step 1: Write failing argv and cleanup tests**

```c
void test_encoder_uses_exec_argv_redacts_url_and_restarts_on_resolution_change(void) {
    struct df_media_encoder_process process = {0};
    struct fake_spawn fake = {0};
    TEST_ASSERT_INT_EQ(DF_OK, start_fake_encoder(&process, &fake, 480, 640));
    TEST_ASSERT_STR_EQ("ffmpeg", fake.argv[0]);
    TEST_ASSERT_STR_EQ("rtsp", argument_after(fake.argv, "-f"));
    TEST_ASSERT_FALSE(text_contains(process.last_error, "secret"));
    TEST_ASSERT_TRUE(df_media_encoder_requires_restart(&process, 360, 480));
}

void test_encoder_stop_closes_pipe_reaps_child_and_invalidates_generation(void) {
    /* fake waitpid observes SIGTERM then SIGKILL only after bounded timeout */
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -B build/doorfast-tests`

Expected: missing encoder supervisor.

- [ ] **Step 3: Implement direct argv spawn**

Build the destination internally from individually validated host, port, stream, user, and password. Pass only this argv shape to `execvp`:

```c
char *argv[] = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "image2pipe", "-vcodec", "mjpeg", "-framerate", fps_text,
    "-i", "pipe:0", "-an", "-pix_fmt", "yuv420p", "-profile:v", profile_text, "-bf", "0", "-g", gop_text,
    "-b:v", bitrate_text, "-maxrate", maxrate_text, "-bufsize", buffer_text, "-x264-params", "repeat-headers=1:scenecut=0",
    "-f", "rtsp", "-rtsp_transport", "tcp", "-rtsp_flags", "send_bye", destination, NULL};
```

For software add `-c:v libx264 -preset veryfast -tune zerolatency`; for VAAPI/QSV add their exact selected codec arguments after a capability probe. Set write end nonblocking, treat `EAGAIN` as a queue retry, `EPIPE` as `encoder_exited`, and never print `destination` or argv. On resolution change stop/reap then start a new RTP session. The module owns all child PIDs; cleanup is idempotent.

- [ ] **Step 4: Run tests**

Run: `make -B test`

Expected: no shell process appears, errors are redacted, child cleanup completes, and resolution restarts exactly once.

- [ ] **Step 5: Commit the unit**

```bash
git add src/media_encoder.[ch] tests/test_media_encoder.c tests/Makefile
git commit -m "feat: supervise H264 RTSP preview encoder"
```

### Task 6: Media Module ABI, Event Relay, and Runtime Integration

**Files:**
- Create: `src/media_module.h`
- Create: `src/media_module.c`
- Create: `src/runtime_media_module.h`
- Create: `src/runtime_media_module.c`
- Create: `src/media_relay.h`
- Create: `src/media_relay.c`
- Modify: `src/gvs_udp_sender.[ch]`
- Modify: `src/runtime_service.c`
- Create: `tests/test_media_module.c`
- Create: `tests/test_runtime_media_module.c`
- Create: `tests/test_media_relay.c`
- Modify: `tests/Makefile`

**Interfaces:**
- Base exports `int df_gvs_udp_sender_emit_control(struct df_gvs_udp_sender *, const uint8_t destination[6], const uint8_t source[6], uint8_t family, uint8_t opcode, const uint8_t *payload, size_t payload_length);`.
- Module ABI symbol is `const struct df_media_module_api_v1 df_media_module_api_v1;`.
- Loader exposes `df_runtime_media_module_start`, `df_runtime_media_module_command`, `df_runtime_media_module_receive_control`, `df_runtime_media_module_push_jpeg`, `df_runtime_media_module_preempt`, and `df_runtime_media_module_status`.

- [ ] **Step 1: Write failing ABI and ordering tests**

```c
void test_runtime_module_loads_only_fixed_abi_and_fails_closed_when_missing(void) {
    struct df_runtime_media_module module = {0};
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_runtime_media_module_start(&module, &enabled_config, &callbacks));
    TEST_ASSERT_FALSE(module.available);
}

void test_incoming_call_preempts_preview_before_call_state_changes(void) {
    /* trace: module stop -> monitor_preempted relay -> normal 03/01 receive */
}

void test_relay_event_contains_only_contract_fields_and_redacted_status(void) {
    /* assert schema_version/event_id/generation/status_revision/timestamp/status; no password, URL credentials, SDP, ICE, or raw payload */
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -B build/doorfast-tests`

Expected: missing module loader and relay interfaces.

- [ ] **Step 3: Implement a fixed module boundary and protocol handoff**

The base loads exactly `/usr/lib/doorfast/media-v1.so` with `RTLD_NOW|RTLD_LOCAL`, checks `abi_version == 1`, and refuses startup when media is enabled but the module is absent or incompatible. The callback accepts fixed parsed fields, not arbitrary functions or shell strings. Extend the UDP sender to serialize with `df_gvs_control_serialize`, resolve only its observed route or configured IPv4 fallback, and send to UDP/8300.

In `runtime_service.c`, pass each valid `07/86` and source IPv4 to the module before normal handling, call `preempt` after a validated incoming `03/01` but before session/media lifecycle updates, pass matching control frames and their source IPv4 to the module, and call `push_jpeg` immediately after `df_gvs_jpeg_validate` and before `df_gvs_video_frame_cache_store`. A failed or slow module call must not block capture; module callbacks are nonblocking and only enqueue work.

Deliver monitor events using a fixed bounded relay queue: `monitor_requested`, `monitor_confirmed`, `monitor_media_ready`, `monitor_publishing`, `monitor_failed`, `monitor_stopped`, `monitor_preempted`. Post JSON with bearer authentication through libcurl to a URL whose scheme is exactly `http` or `https`; `http` is supported for the user's isolated LAN. Limit each request to five seconds, one in flight, three retries, and never print the URL query, token, JSON body, or transport headers.

- [ ] **Step 4: Run native tests with sanitizers**

Run: `make -B test && make clean && make CPPFLAGS='-fsanitize=address,undefined' CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -g' test`

Expected: unit suite passes twice, including queue ownership, stale packet rejection, module absence, incoming-call ordering, and relay non-disclosure.

- [ ] **Step 5: Commit the unit**

```bash
git add src/media_module.[ch] src/runtime_media_module.[ch] src/media_relay.[ch] src/gvs_udp_sender.[ch] src/runtime_service.c tests/test_media_module.c tests/test_runtime_media_module.c tests/test_media_relay.c tests/Makefile
git commit -m "feat: integrate active preview media module"
```

### Task 7: Ubus, CGI, and Credentials Control Contract

**Files:**
- Modify: `src/runtime_ubus.h`
- Modify: `src/runtime_ubus.c`
- Modify: `package/doorfast/files/doorfast-http.sh`
- Modify: `tests/test_runtime_ubus.c`
- Modify: `tests/test_doorfast_http.sh`

**Interfaces:**
- Produces ubus `monitor_start {}`, `monitor_stop { generation }`, `monitor_viewer { generation, active }`, `monitor_status {}`, and `media_credentials { rtsp_password?, relay_token?, clear_rtsp_password?, clear_relay_token? }`.
- Produces CGI `POST /api/v1/monitor/start`, `POST /api/v1/monitor/stop`, `POST /api/v1/monitor/viewer`, and `GET /api/v1/monitor/status`.

- [ ] **Step 1: Write failing generation and redaction tests**

```c
void test_monitor_ubus_rejects_stale_stop_and_reports_no_secrets(void) {
    struct df_runtime_media_status status;
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_runtime_ubus_monitor_stop(&service, 6));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_read_media_status(&service, &status));
    TEST_ASSERT_TRUE(status.rtsp_password_set);
    TEST_ASSERT_FALSE(status.has_credential_text);
}
```

```sh
run /api/v1/monitor/start ''
run /api/v1/monitor/stop '{"generation":7}'
grep -Fxq 'call doorfast monitor_start {}' "$trace"
grep -Fxq 'call doorfast monitor_stop {"generation":7}' "$trace"
! grep -F 'secret-' "$trace"
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -B test`

Expected: monitor methods are not registered and CGI returns unknown endpoint.

- [ ] **Step 3: Implement fixed command validation**

`monitor_start` takes no peer, port, route, URL, or encoder argument. `monitor_stop` and `monitor_viewer` require the exact nonzero current generation; `monitor_viewer` accepts only a blob boolean. The credential action accepts the documented string fields, treats an absent or empty value as preserve, and clears only when the paired `clear_*` boolean is true. Return `queued`/`stopping`/`unavailable` states rather than claims about a physical device or a WebRTC viewer.

- [ ] **Step 4: Run tests**

Run: `make -B test`

Expected: stale and malformed requests fail; CGI does not route arbitrary ubus methods; secret text is absent from every response and trace.

- [ ] **Step 5: Commit the unit**

```bash
git add src/runtime_ubus.[ch] package/doorfast/files/doorfast-http.sh tests/test_runtime_ubus.c tests/test_doorfast_http.sh
git commit -m "feat: expose bounded preview media controls"
```

### Task 8: Optional APK and LuCI Media Configuration

**Files:**
- Create: `package/doorfast-media/Makefile`
- Modify: `package/doorfast/Makefile`
- Modify: `package/luci-app-doorfast/Makefile`
- Modify: `package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json`
- Modify: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js`
- Modify: `package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js`
- Modify: `tests/test_package_manifest.sh`
- Create: `tests/js/test_media_status.mjs`

**Interfaces:**
- `doorfast-media` installs only `/usr/lib/doorfast/media-v1.so`; it has no init script or listener.
- LuCI writes public UCI values and calls ubus only for credentials.

- [ ] **Step 1: Write failing package and UI-model tests**

```sh
grep -F 'DEPENDS:=+doorfast +ffmpeg +libffmpeg-full +libcurl' package/doorfast-media/Makefile
! find package/doorfast-media -name '*init' -print -quit | grep .
```

```js
assert.deepEqual(formatMediaStatus({available:true, state:'publishing', generation:9,
  effective_capacity:1, queue_drops:2, rtsp_password_set:true}), [
  ['State', 'Publishing'], ['Generation', '9'], ['Effective capacity', '1'], ['Queue drops', '2'], ['RTSP password', 'Set']
]);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make -B test && node tests/js/test_media_status.mjs`

Expected: package and formatter assertions fail.

- [ ] **Step 3: Add the optional package and constrained LuCI form**

Compile the module as `-fPIC -shared` with no unresolved base symbols except the declared callback ABI. Add `+libcurl` and `+ca-bundle` for HTTPS trust; do not force a QSV/VAAPI driver. Build package metadata with `+ffmpeg +libffmpeg-full`, and add CI SDK configuration checks that `CONFIG_PACKAGE_libx264=y` for the software path.

In LuCI use `form.Map('doorfast')` and one `form.NamedSection('main', 'gvs')`. Show Media only when package capability status says available. Add exact choices/ranges from the spec, a six-byte station address field, optional IPv4 fallback, host-only validation, a password input that is empty after load, an explicit checkbox for each clear action, and redacted status. Do not make LuCI a second control surface for answer/hangup/unlock/elevator.

- [ ] **Step 4: Run package and JavaScript tests**

Run: `make -B test && node tests/js/test_media_status.mjs && sh tests/test_package_manifest.sh`

Expected: media package has no daemon lifecycle duplication; UI validation rejects a URL in the host field and never renders saved credential content.

- [ ] **Step 5: Commit the unit**

```bash
git add package/doorfast-media package/doorfast/Makefile package/luci-app-doorfast tests/test_package_manifest.sh tests/js/test_media_status.mjs
git commit -m "feat: package and configure preview media"
```

### Task 9: ImmortalWrt VM and go2rtc Ingest Acceptance

**Files:**
- Create: `tests/run_doorfast_vm_media.py`
- Create: `tests/support/fake_ffmpeg.py`
- Create: `tests/support/rtsp_announce_fixture.py`
- Modify: `docs/gvs-vm-udp-validation.md`
- Create: `docs/doorfast-ha-go2rtc-setup.md`

**Interfaces:**
- Consumes built `doorfast` and `doorfast-media` APKs plus the existing VM SSH wrapper.
- Produces a redacted acceptance directory containing status JSON, fake RTSP transaction result, process count, and no packet payload/credential data.

- [ ] **Step 1: Write the VM runner assertions before implementation**

```python
assert status["media"]["state"] == "publishing"
assert status["media"]["generation"] == 1
assert rtsp_fixture.announced_path == "/doorfast_preview"
assert rtsp_fixture.producer_count == 1
assert vm.command("pgrep -fc '[f]fmpeg'") == "1"
inject_incoming_call()
assert status_after["media"]["state"] == "idle"
assert vm.command("pgrep -fc '[f]fmpeg' || true") == "0"
```

- [ ] **Step 2: Run it to verify it fails before the fixture exists**

Run: `python3 -B tests/run_doorfast_vm_media.py /absolute/path/to/vm/ssh.sh /absolute/path/to/doorfast.apk /absolute/path/to/doorfast-media.apk`

Expected: failure that the media APK or RTSP fixture is unavailable.

- [ ] **Step 3: Implement repeatable acceptance**

Install both APKs, configure a fixed active-host test identity, media target `32:02:01:00:02:00`, and only a loopback RTSP fixture address. Inject a recorded `03/84` and JPEG fragments after start, verify the fake server observed RTSP `ANNOUNCE` then `RECORD`, and confirm that a `03/01` stops the encoder before normal incoming-call state. Repeat with bad route, wrong `03/84`, absent first JPEG, unavailable `libx264`, and child exit. Compare `ip rule`, routes, firewall, and listening sockets before/after; only the known fixture connection is allowed.

Document the exact HA go2rtc ingress configuration, including an empty `doorfast_preview:` stream, authenticated `8554`, protected `1984`, and `8555/TCP+UDP` for viewers. State that successful transport proves publication, not physical door-station acceptance.

- [ ] **Step 4: Run native, package, and VM acceptance**

Run: `make -B test && python3 -B tests/run_doorfast_vm_media.py /absolute/path/to/vm/ssh.sh /absolute/path/to/doorfast.apk /absolute/path/to/doorfast-media.apk`

Expected: all failure cases clean up resources, happy path has one producer, and preemption preserves the normal call path.

- [ ] **Step 5: Commit the unit**

```bash
git add tests/run_doorfast_vm_media.py tests/support/fake_ffmpeg.py tests/support/rtsp_announce_fixture.py docs/gvs-vm-udp-validation.md docs/doorfast-ha-go2rtc-setup.md
git commit -m "test: validate preview media on ImmortalWrt"
```

## Plan Self-Review

- Spec coverage: Tasks 1-2 implement evidenced monitor control and route/generation admission. Tasks 3-5 implement safe configuration, credentials, capacity, H.264 and RTSP. Tasks 6-7 integrate lifecycle, relay, ubus, CGI, and preemption. Task 8 implements optional packaging and LuCI. Task 9 covers VM/go2rtc acceptance, including cleanup and non-disclosure.
- Added design correction: `media_station_address` is an exact captured six-byte door-station identity and `media_station_ipv4` is the explicit fallback. The existing `IS:` parser is never reused for a door-station target.
- Placeholder scan: no deferred implementation markers or unspecified interfaces remain. The API names used by later tasks are declared in the task that creates them.
- Type consistency: monitor generation is `uint64_t`, selected media capacity is `unsigned`, public ports are `uint16_t`, and every endpoint accepting a generation requires that nonzero `uint64_t` value.

## Execution Handoff

Execute inline in this session using `superpowers:executing-plans`, task by task with the tests and review gates above. The HA implementation is covered by the companion plan `docs/superpowers/plans/2026-09-17-doorfastforha-webrtc-camera.md`.
