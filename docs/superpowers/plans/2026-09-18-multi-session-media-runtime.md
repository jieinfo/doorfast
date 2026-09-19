# Multi-Session Media Runtime Implementation Plan

> **For AI agent workers:** Required sub-skill: use
> `subagent-driven-development` (recommended) or `executing-plans` to implement
> this plan task by task. Track progress with the checkboxes below.

**Goal:** Replace media ABI v2's singleton station/monitor/encoder with ABI v3,
support a LuCI-selected number of simultaneous station source streams, and
expose station-scoped monitor operations to Home Assistant.

**Architecture:** The media module owns a dynamically allocated session manager
whose slots are keyed by stable station ID and runtime-unique generation. Each
active session owns protocol state, JPEG reassembly, a frame queue, and one
supervised FFmpeg publisher. The core demultiplexes control and video by exact
station address, while a global admission policy enforces configured capacity,
memory, and incoming-call priority.

**Technical stack:** C17 shared-module ABI, `dlopen`/`dlsym`, POSIX processes
and pipes, FFmpeg, RTSP/TCP, libubus/blobmsg, shell HTTP bridge, Python VM
fixtures.

**Spec:**
`docs/superpowers/specs/2026-09-18-multi-station-media-design.md`

**Prerequisite:** Complete and merge
`docs/superpowers/plans/2026-09-18-station-discovery-luci.md` first.

## Global Constraints

- `max_encoders` defaults to 1 and may not exceed enabled station count when
  media is enabled.
- No hidden hardware-specific encoder cap is allowed.
- One station source consumes one slot; additional viewers of that same station
  consume no additional source slots.
- Every media mutation is bound to `runtime_id + station_id + generation`.
- A failed session must not stop or corrupt another station session.
- `preempt_oldest_preview` is the default incoming-call policy;
  `preserve_previews` is supported.
- Call sessions are never preempted to admit a proactive preview.
- `submitted` and `ready` remain protocol/runtime states, not physical-action
  claims.
- ABI v2 is rejected explicitly; `doorfast` and `doorfast-media` upgrade
  together.
- PR titles and commits are in English.

---

## File Structure

**Create:**

- `src/media_session.h`: station-scoped session key, purpose, state, counters,
  and failure codes.
- `src/media_session.c`: one session's protocol, queue, encoder, and cleanup
  lifecycle.
- `src/media_session_manager.h`: dynamic pool, capacity, lookup, admission,
  preemption, status snapshots, and generation allocation.
- `src/media_session_manager.c`: cross-session arbitration and failure
  isolation.
- `tests/test_media_session.c`: per-session lifecycle and frame isolation.
- `tests/test_media_session_manager.c`: capacity, reuse, preemption, memory,
  and generation tests.
- `tests/support/media_v3_acceptance.c`: host harness linked to the APK media
  source set and driven only through the exported ABI v3 API.
- `tests/run_doorfast_vm_multi_media.py`: VM install preflight plus host ABI v3
  acceptance for simultaneous encoders and stream cleanup.

**Modify:**

- `src/media_module.h`: ABI v3 types, configuration, commands, errors, status,
  and exported symbol.
- `src/media_module.c`: thin ABI facade over the session manager.
- `src/runtime_media_module.h`, `src/runtime_media_module.c`: ABI v3 loader and
  station-scoped calls.
- `src/media_encoder.h`, `src/media_encoder.c`: allow one independent process
  object per session and preserve station stream paths.
- `src/media_frame_queue.h`, `src/media_frame_queue.c`: session-local queue
  reset and generation checks.
- `src/gvs_monitor.h`, `src/gvs_monitor.c`: no singleton assumptions in
  monitor control state.
- `src/runtime_config.h`, `src/runtime_config.c`: real `media_max_encoders` and
  incoming-call policy.
- `src/runtime_service.c`: build ABI v3 config, station-address demultiplexing,
  call transition, and event publication.
- `src/runtime_ubus.h`, `src/runtime_ubus.c`: keyed monitor operations and
  session-array status.
- `package/doorfast/files/doorfast-http.sh`: keyed monitor JSON contracts.
- `package/doorfast/files/doorfast.config`: capacity 1 and call-priority
  defaults.
- `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/media.js`:
  capacity and call-priority fields.
- `package/doorfast/Makefile`, `package/doorfast-media/Makefile`: ABI v3 sources
  and matched package revisions.
- `tests/test_media_module.c`, `tests/test_runtime_media_module.c`,
  `tests/test_media_encoder.c`, `tests/test_media_frame_queue.c`,
  `tests/test_runtime_config.c`, `tests/test_runtime_service.c`,
  `tests/test_runtime_ubus.c`, `tests/test_doorfast_http.sh`,
  `tests/test_luci_media.js`, `tests/test_package_manifest.sh`, `tests/test_main.c`,
  `Makefile`: regression and contract coverage.
- `README.md`: ABI v3, resource, capacity, concurrency, and evidence status.

---

### Task 1: Freeze the ABI v3 Contract

**Files:**

- Modify: `src/media_module.h`
- Modify: `tests/test_media_module.c`
- Modify: `tests/test_runtime_media_module.c`

- [ ] **Step 1: Write compile-time and runtime ABI tests**

Replace v2 test fixtures with two stations and require the loader to reject an
API table whose `abi_version` is 2. Define expected public shapes in tests:

```c
struct df_media_station_config_v3 stations[2] = {
    {.id = "gate_main", .stream_name = "doorfast_gate_main"},
    {.id = "gate_side", .stream_name = "doorfast_gate_side"},
};
struct df_media_module_config_v3 config = {
    .stations = stations,
    .station_count = 2U,
    .max_encoders = 2U,
    .incoming_call_policy = DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW,
};
```

- [ ] **Step 2: Run tests and confirm ABI v3 is missing**

Run `make clean && make test`.

Expected: compilation fails because v3 types and `df_media_module_api_v3` do
not exist.

- [ ] **Step 3: Define exact ABI v3 public types**

Add:

```c
#define DF_MEDIA_MODULE_ABI_VERSION 3U

enum df_media_session_purpose {
    DF_MEDIA_SESSION_PREVIEW = 0,
    DF_MEDIA_SESSION_CALL,
};

enum df_media_call_policy {
    DF_MEDIA_CALL_PREEMPT_OLDEST_PREVIEW = 0,
    DF_MEDIA_CALL_PRESERVE_PREVIEWS,
};

struct df_media_session_key {
    const char *station_id;
    uint64_t generation;
};

struct df_media_module_api_v3 {
    uint32_t abi_version;
    size_t struct_size;
    void *(*create)(const struct df_media_module_config_v3 *,
                    const struct df_media_module_callbacks_v3 *);
    void (*destroy)(void *);
    int (*start)(void *, const char *, enum df_media_session_purpose,
                 uint64_t, uint64_t);
    int (*command)(void *, enum df_media_module_command,
                   const struct df_media_session_key *, bool, uint64_t);
    int (*receive_control)(void *, const struct df_gvs_frame *,
                           uint32_t, uint64_t);
    int (*push_jpeg)(void *, const uint8_t[6], const uint8_t[6], uint32_t,
                     const uint8_t *, size_t, uint16_t, uint16_t, uint64_t);
    int (*tick)(void *, uint64_t);
    int (*status)(const void *, struct df_media_module_status_v3 *);
};
```

Define concrete config, callbacks, status capacities, and ownership rules in
the header. Status snapshots are caller-owned buffers: first query required
session count, then fill a caller-provided array. Do not expose module-owned
pointers after the call returns.

- [ ] **Step 4: Run ABI-focused and full C tests**

Run `make clean && make test`.

Expected: ABI table validation passes for v3 and rejects v2, short
`struct_size`, null required callbacks, duplicate station IDs, and capacity
above station count.

- [ ] **Step 5: Commit**

```bash
git add src/media_module.h tests/test_media_module.c \
  tests/test_runtime_media_module.c
git commit -m "feat: define media module ABI v3"
```

### Task 2: Station-Scoped Session and Pool Admission

**Files:**

- Create: `src/media_session.h`
- Create: `src/media_session.c`
- Create: `src/media_session_manager.h`
- Create: `src/media_session_manager.c`
- Create: `tests/test_media_session.c`
- Create: `tests/test_media_session_manager.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

- [ ] **Step 1: Write failing pool behavior tests**

Cover same-station reuse, two-station admission, third-station busy, disabled
station rejection, runtime-unique generations, memory rejection, encoder-start
rollback, and lookup by key:

```c
uint64_t first_generation = 0U;
uint64_t reused_generation = 0U;

TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
    &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 100U,
    &first_generation));
TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_start(
    &manager, "gate_main", DF_MEDIA_SESSION_PREVIEW, 101U,
    &reused_generation));
TEST_ASSERT_INT_EQ(first_generation, reused_generation);
TEST_ASSERT_INT_EQ(1, df_media_session_manager_active(&manager));
```

- [ ] **Step 2: Run tests and confirm missing manager API**

Run `make clean && make test`.

Expected: compilation fails on `media_session_manager.h`.

- [ ] **Step 3: Implement dynamic sessions and atomic admission**

Allocate session slots from effective capacity, not from a fixed compile-time
array. Allocate a generation only after route and memory admission succeed.
Reserve the slot, start session resources, then publish it as active. On any
failure, destroy partial resources and return the stable error code without
incrementing active count.

Expose:

```c
int df_media_session_manager_init(struct df_media_session_manager *,
    const struct df_media_module_config_v3 *,
    const struct df_media_module_callbacks_v3 *);
int df_media_session_manager_start(struct df_media_session_manager *,
    const char *station_id, enum df_media_session_purpose, uint64_t now_ms,
    uint64_t *generation);
int df_media_session_manager_command(struct df_media_session_manager *,
    enum df_media_module_command, const struct df_media_session_key *,
    bool active, uint64_t now_ms);
```

- [ ] **Step 4: Run pool tests and full suite**

Run `make clean && make test`.

Expected: all C tests exit 0; a failed second session leaves the first session
active and unchanged.

- [ ] **Step 5: Commit**

```bash
git add src/media_session.* src/media_session_manager.* \
  tests/test_media_session.c tests/test_media_session_manager.c \
  tests/test_main.c Makefile
git commit -m "feat: manage concurrent media sessions"
```

### Task 3: Independent Encoder and JPEG Pipelines

**Files:**

- Modify: `src/media_session.c`
- Modify: `src/media_encoder.h`
- Modify: `src/media_encoder.c`
- Modify: `src/media_frame_queue.h`
- Modify: `src/media_frame_queue.c`
- Modify: `src/media_module.c`
- Modify: `tests/test_media_session.c`
- Modify: `tests/test_media_encoder.c`
- Modify: `tests/test_media_frame_queue.c`
- Modify: `tests/test_media_module.c`

- [ ] **Step 1: Write failing frame-isolation tests**

Start two sessions with distinct logical addresses and stream names. Push an
interleaved JPEG sequence and assert separate encoder captures:

```c
TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
    &manager, gate_main, local, main_ipv4, jpeg_a, sizeof(jpeg_a),
    480U, 640U, 200U));
TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_push_jpeg(
    &manager, gate_side, local, side_ipv4, jpeg_b, sizeof(jpeg_b),
    480U, 640U, 201U));
TEST_ASSERT_INT_EQ(1, captures[0].frames);
TEST_ASSERT_INT_EQ(1, captures[1].frames);
```

Also prove that a frame from an unknown station, wrong destination, or stale
route reaches neither queue.

- [ ] **Step 2: Run tests and confirm singleton behavior fails**

Run `make clean && make test`.

Expected: tests fail because the current module owns one queue and one encoder.

- [ ] **Step 3: Move encoder and queue ownership into each session**

Build the RTSP path from that session's immutable `stream_name`. Keep process
PID, pipe, counters, and last error inside `struct df_media_session`. Route
frames by exact six-byte source address. Reset only the target session on
dimension change, encoder exit, or generation transition.

- [ ] **Step 4: Verify interleaving, exit, and cleanup**

Run `make clean && make test`.

Expected: all tests exit 0; killing one fake FFmpeg process moves only its
session to failed and the other capture continues accepting frames.

- [ ] **Step 5: Commit**

```bash
git add src/media_session.c src/media_encoder.* src/media_frame_queue.* \
  src/media_module.c tests/test_media_session.c tests/test_media_encoder.c \
  tests/test_media_frame_queue.c tests/test_media_module.c
git commit -m "feat: isolate station encoder pipelines"
```

### Task 4: Monitor Protocol and Incoming-Call Priority

**Files:**

- Modify: `src/gvs_monitor.h`
- Modify: `src/gvs_monitor.c`
- Modify: `src/media_session.c`
- Modify: `src/media_session_manager.c`
- Modify: `src/runtime_service.c`
- Modify: `tests/test_gvs_monitor.c`
- Modify: `tests/test_media_session_manager.c`
- Modify: `tests/test_runtime_service.c`

- [ ] **Step 1: Write failing protocol concurrency tests**

Require two sessions to emit independent `03/04`, correlate only their own
`03/84`, and stop with their own `03/02`/`03/82`. Add call-priority cases:

```c
TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_incoming_call(
    &manager, "gate_call", 44U, 500U));
TEST_ASSERT_INT_EQ(0, strcmp("gate_preview_old",
    capture.preempted_station_id));
TEST_ASSERT_INT_EQ(DF_MEDIA_SESSION_CALL,
    df_media_session_manager_find(&manager, "gate_call")->purpose);
```

For preserve mode, require `DF_MEDIA_ERR_CAPACITY_BUSY` and prove the incoming
call state machine still accepts answer/hangup controls.

- [ ] **Step 2: Run tests and confirm missing priority behavior**

Run `make clean && make test`.

Expected: concurrency and call-priority tests fail.

- [ ] **Step 3: Implement keyed monitor correlation and preemption**

Give every monitor state a station address and generation. On same-station
incoming call, cancel proactive monitor deadlines, clear queued preview frames,
retain the capacity slot and stream name, and bind to the call generation. On a
different station at capacity, select the active preview with the smallest
`started_ms`; never select a call session. Emit `monitor_preempted` with victim
station and generation.

- [ ] **Step 4: Run protocol and full tests**

Run `make clean && make test`.

Expected: all C tests exit 0, including out-of-order replies and stale
generation rejection.

- [ ] **Step 5: Commit**

```bash
git add src/gvs_monitor.* src/media_session.c src/media_session_manager.c \
  src/runtime_service.c tests/test_gvs_monitor.c \
  tests/test_media_session_manager.c tests/test_runtime_service.c
git commit -m "feat: prioritize incoming call media"
```

### Task 5: Runtime Loader, Configuration, ubus, and HTTP

**Files:**

- Modify: `src/runtime_media_module.h`
- Modify: `src/runtime_media_module.c`
- Modify: `src/runtime_config.h`
- Modify: `src/runtime_config.c`
- Modify: `src/runtime_service.c`
- Modify: `src/runtime_ubus.h`
- Modify: `src/runtime_ubus.c`
- Modify: `package/doorfast/files/doorfast-http.sh`
- Modify: `package/doorfast/files/doorfast.config`
- Modify: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/media.js`
- Modify: `tests/test_runtime_media_module.c`
- Modify: `tests/test_runtime_config.c`
- Modify: `tests/test_runtime_ubus.c`
- Modify: `tests/test_doorfast_http.sh`
- Modify: `tests/test_luci_media.js`

- [ ] **Step 1: Write failing keyed API tests**

Require monitor start, stop, and viewer requests to include exact identity
fields. For example:

```json
{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":21,"active":true}
```

Require status fields `configured_capacity`, `effective_capacity`,
`active_encoders`, and a session array. Test `runtime_mismatch`,
`station_not_found`, `generation_mismatch`, `capacity_busy`, and
`resource_exhausted` response codes.

- [ ] **Step 2: Run focused failures**

Run:

```bash
make clean && make test
sh tests/test_doorfast_http.sh
node tests/test_luci_media.js
```

Expected: v2 loader and empty monitor-start contract fail the new assertions.

- [ ] **Step 3: Connect ABI v3 through every boundary**

Resolve only `df_media_module_api_v3` with `dlsym`. Build the module config from
the station registry. Parse `media_max_encoders` as an unsigned value bounded
by enabled station count when media is enabled. Parse call policy values
`preempt_oldest_preview` and `preserve_previews`.

Change runtime wrappers to:

```c
int df_runtime_media_module_request_start(struct df_runtime_media_module *,
    const char *station_id, uint64_t now_ms, uint64_t *generation);
int df_runtime_media_module_command(struct df_runtime_media_module *,
    enum df_media_module_command, const struct df_media_session_key *,
    bool active, uint64_t now_ms);
```

Require `runtime_id` in ubus and HTTP before invoking either wrapper. Return
machine-readable JSON errors without FFmpeg command lines or credentials.

- [ ] **Step 4: Run C, HTTP, and LuCI tests**

Run:

```bash
make clean && make test
sh tests/test_doorfast_http.sh
node tests/test_luci_media.js
```

Expected: every command exits 0.

- [ ] **Step 5: Commit**

```bash
git add src/runtime_media_module.* src/runtime_config.* src/runtime_service.c \
  src/runtime_ubus.* package/doorfast/files/doorfast-http.sh \
  package/doorfast/files/doorfast.config \
  package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/media.js \
  tests/test_runtime_media_module.c tests/test_runtime_config.c \
  tests/test_runtime_ubus.c tests/test_doorfast_http.sh tests/test_luci_media.js
git commit -m "feat: expose station scoped media sessions"
```

### Task 6: Package Lifecycle and VM Multi-Stream Acceptance

**Files:**

- Create: `tests/support/media_v3_acceptance.c`
- Create: `tests/run_doorfast_vm_multi_media.py`
- Modify: `package/doorfast/Makefile`
- Modify: `package/doorfast-media/Makefile`
- Modify: `tests/test_package_manifest.sh`
- Modify: `tests/test_vm_media_runner.py`
- Modify: `README.md`, the repository's single authoritative status document.

- [ ] **Step 1: Write a failing two-stream VM acceptance**

The compiled host fixture must use exported ABI v3 `start`, `tick`,
`receive_control`, `push_jpeg`, and `command` calls. It records each preview's
`03/04`, injects matching synthetic `03/84` replies, requires stop/preemption to
emit `03/02`, feeds different JPEG markers, captures two RTSP ANNOUNCE paths,
stops one session, and proves the other stays active. The two scenarios must be
fully separated so the observed RTSP producer peak is exactly two. The VM
portion is an installed-module preflight only; do not describe the host fixture
as VM daemon injection. Require a result object with:

```json
{"configured_capacity":2,"peak_active_encoders":2,"streams":["doorfast_gate_main","doorfast_gate_side"],"isolated_stop":true}
```

- [ ] **Step 2: Run the fake VM acceptance and confirm failure**

Run:

```bash
python3 -B tests/run_doorfast_vm_multi_media.py \
  tests/fixtures/fake-vm-preflight-ssh.sh
```

Expected: failure until the runtime and fixture support two sessions.

- [ ] **Step 3: Complete packaging and acceptance runner**

Install ABI v3 module sources in `doorfast-media`, bump core/media package
revisions together, and make upgrade restart the core only after the new module
is in place. The runner records ABI status, control counts, RTSP metadata,
streams, memory floor, per-session state, and cleanup. The fake encoder records
only per-path byte counts and SHA-256 digests so the test can prove pipe
isolation without retaining media payloads or credentials. Add a
call-at-capacity fixture for both priority modes.

- [ ] **Step 4: Run full main-project verification**

Run:

```bash
make clean && make test
sh tests/test_package_manifest.sh
sh tests/test_doorfast_http.sh
node tests/test_luci_media.js
python3 -B tests/run_doorfast_vm_multi_media.py \
  tests/fixtures/fake-vm-preflight-ssh.sh
git diff --check
```

Expected: every command exits 0. Documentation labels concurrent device
acceptance as pending until a field PCAP demonstrates two successful `03/04`
sessions and separate UDP/8303 sources.

- [ ] **Step 5: Commit**

```bash
git add package/doorfast package/doorfast-media tests README.md docs
git commit -m "test: validate concurrent media sessions"
```

After this plan, request review and merge the main-project PR. Wait for its
Actions result from the user and do not poll. Begin the HA plan only after the
station and ABI v3 HTTP contracts are merged.
