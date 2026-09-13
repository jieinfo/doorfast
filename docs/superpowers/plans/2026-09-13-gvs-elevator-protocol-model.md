# GVS Elevator Protocol Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a network-free GVS elevator codec and bounded call-elevator transaction controller that exactly reproduces the vendor PCAP request layout and never equates a protocol reply with physical elevator movement.

**Architecture:** `gvs_elevator` owns address derivation, `08/02` and `08/03` serialization, and `08/83` parsing. `gvs_elevator_control` owns one active request, the immediate/1000 ms send schedule, `08/82` correlation, terminal states, and monotonic-time validation. Runtime UDP, ubus, LuCI, package releases, and automatic state polling remain outside this plan.

**Tech Stack:** C17, existing `df_gvs_control_serialize` and `df_gvs_frame`, project test runner, Clang/GCC warning-clean flags.

**Spec:** `docs/superpowers/specs/2026-09-13-gvs-elevator-protocol-design.md`

## Global Constraints

- `08/02` is exactly 46 bytes: 42-byte public header plus a 4-byte payload.
- `08/03` is exactly 42 bytes with an empty payload.
- Call payload is `direction, decode_packed_bcd(local[3]), local[3], local[4]`.
- Destination is `35 local[1] local[2] 00 01 00`; source must be a valid type-`61` indoor identity.
- Direction accepts only `0` (down) and `1` (up).
- At most two call sends occur: immediately and at 1000 ms; the transaction ends no later than 2000 ms.
- `08/82` means protocol completion only; `physical_result_confirmed` remains false.
- `08/83` supports at most 8 entries and preserves raw floor/state values and extension length.
- `08/01`, floor selection, network sending, ubus, LuCI, and HA are not implemented in this plan.
- All production changes follow a witnessed RED → GREEN test cycle.

---

### Task 1: Encode captured call-elevator and status-query requests

**Files:**
- Create: `src/gvs_elevator.h`
- Create: `src/gvs_elevator.c`
- Create: `tests/test_gvs_elevator.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `df_gvs_control_serialize(...)`, `df_gvs_header_provider_fn`, `DF_OK`, `DF_ERR_INVALID`.
- Produces:

```c
#define DF_GVS_ELEVATOR_CALL_FRAME_SIZE 46U
#define DF_GVS_ELEVATOR_QUERY_FRAME_SIZE 42U
#define DF_GVS_ELEVATOR_CALL_PAYLOAD_SIZE 4U

enum df_gvs_elevator_direction {
    DF_GVS_ELEVATOR_DOWN = 0,
    DF_GVS_ELEVATOR_UP = 1,
};

struct df_gvs_elevator_request {
    bool valid;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t opcode;
    uint8_t payload[DF_GVS_ELEVATOR_CALL_PAYLOAD_SIZE];
    uint16_t payload_length;
};

int df_gvs_elevator_prepare_call(
    const uint8_t local[6], enum df_gvs_elevator_direction direction,
    struct df_gvs_elevator_request *request);
int df_gvs_elevator_prepare_query(
    const uint8_t local[6], struct df_gvs_elevator_request *request);
int df_gvs_elevator_serialize(
    const struct df_gvs_elevator_request *request, uint8_t *output,
    size_t capacity, size_t *output_length,
    df_gvs_header_provider_fn provide_fields, void *fields_context);
```

- [ ] **Step 1: Write failing request codec tests**

Add five tests to `tests/test_gvs_elevator.c`. The two-digit capture test must contain these exact assertions:

```c
const uint8_t local[6] = {0x61, 0x02, 0x01, 0x16, 0x01, 0x01};
const uint8_t expected_destination[6] = {0x35, 0x02, 0x01, 0, 1, 0};
const uint8_t expected_payload[4] = {0x00, 0x10, 0x16, 0x01};
struct df_gvs_elevator_request request;
uint8_t wire[DF_GVS_ELEVATOR_CALL_FRAME_SIZE];
size_t wire_length = 0;

TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_prepare_call(
    local, DF_GVS_ELEVATOR_DOWN, &request));
TEST_ASSERT_INT_EQ(0, memcmp(request.destination,
    expected_destination, sizeof(expected_destination)));
TEST_ASSERT_INT_EQ(0, memcmp(request.payload,
    expected_payload, sizeof(expected_payload)));
TEST_ASSERT_INT_EQ(DF_OK, df_gvs_elevator_serialize(
    &request, wire, sizeof(wire), &wire_length,
    df_gvs_placeholder_header_fields, NULL));
TEST_ASSERT_INT_EQ(46, (int)wire_length);
TEST_ASSERT_INT_EQ(0x08, wire[38]);
TEST_ASSERT_INT_EQ(0x02, wire[39]);
TEST_ASSERT_INT_EQ(4, wire[40]);
TEST_ASSERT_INT_EQ(0, wire[41]);
TEST_ASSERT_INT_EQ(0, memcmp(wire + 42, expected_payload, 4));
```

The remaining tests must assert:

- local `61:13:01:06:01:01` produces both captured payloads `00 06 06 01` and `01 06 06 01`;
- query opcode is `03`, payload length is zero, and serialized length is 42;
- output request is cleared for type other than `61`, invalid BCD nibbles, zero identity, and direction value 2;
- insufficient output capacity or a failing header provider sets output length to zero and leaves the caller output buffer unchanged.

Declare and call all five tests in `tests/test_main.c`; change `test_suite_count()` from 128 to 133. Add `tests/test_gvs_elevator.c src/gvs_elevator.c` to `TEST_SOURCES` and `src/gvs_elevator.c` to `DAEMON_SOURCES`.

- [ ] **Step 2: Run the tests to verify RED**

Run:

```bash
make -B test
```

Expected: compilation fails because `gvs_elevator.h` and the declared API do not exist. This is the required RED state; do not add production declarations before observing it.

- [ ] **Step 3: Implement the minimum request codec**

Create the header with the exact public interface above. In `src/gvs_elevator.c`, validate packed BCD with both nibbles at most 9 and decode it as:

```c
static int df_gvs_elevator_bcd_decode(uint8_t value, uint8_t *decoded) {
    uint8_t high = (uint8_t)(value >> 4);
    uint8_t low = (uint8_t)(value & 0x0fU);

    if (decoded == NULL || high > 9U || low > 9U)
        return DF_ERR_INVALID;
    *decoded = (uint8_t)(high * 10U + low);
    return DF_OK;
}
```

Prepare a call only after validating the five identity bytes after the fixed type byte as packed BCD. Build the request atomically in a zeroed local variable, set destination and payload exactly as specified, then assign it to the caller. Query preparation uses the same identity and target validation but opcode `0x03` and payload length zero.

Serialize only valid opcode `0x02`/length 4 or opcode `0x03`/length 0 combinations:

```c
return df_gvs_control_serialize(
    output, capacity, output_length, request->destination, request->source,
    0x08, request->opcode,
    request->payload_length == 0U ? NULL : request->payload,
    request->payload_length, provide_fields, fields_context);
```

Use a temporary wire buffer before copying to the caller so provider failure and insufficient capacity are output-atomic.

- [ ] **Step 4: Run request codec tests to verify GREEN**

Run:

```bash
make -B test
```

Expected: all 133 tests pass without warnings.

- [ ] **Step 5: Commit the request codec**

```bash
git add Makefile src/gvs_elevator.c src/gvs_elevator.h tests/test_gvs_elevator.c tests/test_main.c
git commit -m "feat: encode GVS elevator requests"
```

### Task 2: Parse bounded elevator status replies

**Files:**
- Modify: `src/gvs_elevator.h`
- Modify: `src/gvs_elevator.c`
- Modify: `tests/test_gvs_elevator.c`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `struct df_gvs_frame` and Task 1 target derivation.
- Produces:

```c
#define DF_GVS_ELEVATOR_MAX_ENTRIES 8U

enum df_gvs_elevator_motion {
    DF_GVS_ELEVATOR_FAULT = 0,
    DF_GVS_ELEVATOR_MOVING_UP = 1,
    DF_GVS_ELEVATOR_MOVING_DOWN = 2,
    DF_GVS_ELEVATOR_STOPPED = 3,
    DF_GVS_ELEVATOR_OTHER = 255,
};

struct df_gvs_elevator_entry {
    int8_t raw_floor;
    int16_t floor;
    uint8_t raw_state;
    enum df_gvs_elevator_motion motion;
};

struct df_gvs_elevator_status {
    bool valid;
    size_t count;
    size_t extension_length;
    struct df_gvs_elevator_entry entries[DF_GVS_ELEVATOR_MAX_ENTRIES];
};

int df_gvs_elevator_parse_status(
    const struct df_gvs_frame *frame, const uint8_t local[6],
    struct df_gvs_elevator_status *status);
const char *df_gvs_elevator_motion_name(enum df_gvs_elevator_motion motion);
```

- [ ] **Step 1: Write failing status parser tests**

Add four tests. The primary case uses a strict reverse route and payload below:

```c
uint8_t payload[] = {2, 5, 1, 6, 3, 0xaa};
struct df_gvs_frame frame = {
    .family = 0x08, .opcode = 0x83,
    .payload = payload, .payload_length = sizeof(payload),
};
```

Set frame source to `35 local[1] local[2] 00 01 00`, destination to local, and assert two entries, states `moving_up` and `stopped`, and `extension_length == 1`.

Add separate cases for raw floors `0x80`, `0x81`, and `0xff`, expecting decoded floors `0`, `-1`, and `-127`; for empty/truncated/count 9 payloads; and for wrong family, opcode, source, or destination. Every rejection must leave an all-zero invalid result. Update `test_suite_count()` from 133 to 137.

- [ ] **Step 2: Run the tests to verify RED**

```bash
make -B test
```

Expected: compilation fails because `df_gvs_elevator_parse_status` and the status types are absent.

- [ ] **Step 3: Implement bounded status parsing**

Validate all pointers, local identity, family `0x08`, opcode `0x83`, exact reverse addresses, nonempty payload, count at most 8, and `payload_length >= 1 + 2 * count`. Parse into a zeroed local result. Convert floors with:

```c
int raw = (int)(int8_t)frame->payload[1U + index * 2U];
entry->raw_floor = (int8_t)raw;
entry->floor = (int16_t)(raw < 0 ? -(raw + 128) : raw);
```

Map raw state 0 through 3 to the named enum and all other values to `DF_GVS_ELEVATOR_OTHER`. Set `extension_length` to the unconsumed payload bytes and assign the completed local result to the caller only after all checks pass.

- [ ] **Step 4: Run status parser tests to verify GREEN**

```bash
make -B test
```

Expected: all 137 tests pass without warnings.

- [ ] **Step 5: Commit the status parser**

```bash
git add src/gvs_elevator.c src/gvs_elevator.h tests/test_gvs_elevator.c tests/test_main.c
git commit -m "feat: parse bounded GVS elevator status"
```

### Task 3: Add the single-slot call-elevator controller

**Files:**
- Create: `src/gvs_elevator_control.h`
- Create: `src/gvs_elevator_control.c`
- Create: `tests/test_gvs_elevator_control.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`
- Modify: `docs/mt8157-vendor-reference-matrix.md`

**Interfaces:**
- Consumes: Task 1 request preparation and `struct df_gvs_frame`.
- Produces:

```c
enum df_gvs_elevator_control_state {
    DF_GVS_ELEVATOR_CONTROL_IDLE,
    DF_GVS_ELEVATOR_CONTROL_WAITING,
    DF_GVS_ELEVATOR_CONTROL_PROTOCOL_COMPLETED,
    DF_GVS_ELEVATOR_CONTROL_EXPIRED,
    DF_GVS_ELEVATOR_CONTROL_SEND_FAILED,
    DF_GVS_ELEVATOR_CONTROL_CANCELLED,
};

typedef int (*df_gvs_elevator_send_fn)(
    const struct df_gvs_elevator_request *request, void *context);

struct df_gvs_elevator_control {
    enum df_gvs_elevator_control_state state;
    struct df_gvs_elevator_request request;
    uint64_t transaction_id;
    uint64_t last_now_ms;
    uint64_t next_send_ms;
    uint64_t deadline_ms;
    unsigned attempts;
    unsigned successful_sends;
    bool physical_result_confirmed;
    df_gvs_elevator_send_fn send;
    void *send_context;
};

int df_gvs_elevator_control_init(
    struct df_gvs_elevator_control *control, uint64_t now_ms,
    df_gvs_elevator_send_fn send, void *send_context);
int df_gvs_elevator_control_submit(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    enum df_gvs_elevator_direction direction, uint64_t transaction_id,
    uint64_t now_ms);
int df_gvs_elevator_control_tick(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    uint64_t now_ms);
int df_gvs_elevator_control_observe(
    struct df_gvs_elevator_control *control, const uint8_t local[6],
    const struct df_gvs_frame *frame, uint64_t now_ms);
const char *df_gvs_elevator_control_state_name(
    enum df_gvs_elevator_control_state state);
```

- [ ] **Step 1: Write failing controller lifecycle tests**

Add five tests using a sender that records request copies and configurable return values. Verify these exact timelines:

```text
submit@0       attempts=1, state=waiting
tick@999       attempts=1, state=waiting
tick@1000      attempts=2, state=waiting
tick@1999      state=waiting
tick@2000      state=expired
```

Other tests must verify matching `08/82` completes before the deadline without inspecting payload and keeps `physical_result_confirmed=false`; duplicate submit and wrong/late replies are rejected without mutation; first-send failure followed by success expires while two failures end at 1000 ms as `send_failed`; changed local identity and backward time are rejected or cancel exactly as the spec states. Update `test_suite_count()` from 137 to 142. Add `tests/test_gvs_elevator_control.c src/gvs_elevator_control.c` to `TEST_SOURCES` and `src/gvs_elevator_control.c` to `DAEMON_SOURCES`.

- [ ] **Step 2: Run the tests to verify RED**

```bash
make -B test
```

Expected: compilation fails because `gvs_elevator_control.h` and the controller API are absent.

- [ ] **Step 3: Implement the controller state machine**

Initialization requires a non-null sender. Submission rejects zero transaction ids, backward time, and an existing `waiting` slot; it prepares the call request, sets `next_send_ms = now_ms + 1000` and `deadline_ms = now_ms + 2000` with overflow checks, records the first send result, and enters `waiting`. A failed first send does not make submission fail because the accepted transaction retains its second scheduled opportunity; submission returns `DF_OK` after recording either sender result.

Tick copies the controller to `next`, applies rules in this order, updates `next.last_now_ms`, and assigns `next` to the caller only on `DF_OK`:

```c
struct df_gvs_elevator_control next = *control;

if (now_ms < next.last_now_ms)
    return DF_ERR_INVALID;
next.last_now_ms = now_ms;
if (next.state != DF_GVS_ELEVATOR_CONTROL_WAITING) {
    *control = next;
    return DF_OK;
}
if (memcmp(local, next.request.source, 6) != 0) {
    next.state = DF_GVS_ELEVATOR_CONTROL_CANCELLED;
    *control = next;
    return DF_OK;
}
if (now_ms >= next.deadline_ms) {
    next.state = DF_GVS_ELEVATOR_CONTROL_EXPIRED;
    *control = next;
    return DF_OK;
}
if (next.attempts == 1U && now_ms >= next.next_send_ms) {
    int sent = next.send(&next.request, next.send_context);
    next.attempts = 2U;
    if (sent == DF_OK)
        next.successful_sends++;
    else if (next.successful_sends == 0U)
        next.state = DF_GVS_ELEVATOR_CONTROL_SEND_FAILED;
}
*control = next;
return DF_OK;
```

Observe first checks monotonic time and `waiting`, then requires at least one successful send, `now_ms < deadline_ms`, family `08`, opcode `82`, source equal to the derived elevator target, and destination equal to local. It sets only `protocol_completed`, clears deadlines, and leaves `physical_result_confirmed` false. All validation must use a local copy so rejected input does not partially advance time or state.

- [ ] **Step 4: Run controller and full project verification**

Run:

```bash
make -B test
make -B doorfast
make -B peer-udp-inject
DF_GVS_TEST_PORT=18301 python3 -B -m unittest tests/test_gvs_peer_udp.py
python3 -B -m unittest tests/test_vm_preflight_state.py tests/test_compare_doorfast_site.py
node tests/test_luci_status.js
sh tests/test_main_cli.sh
sh tests/test_recorder_cli.sh
sh tests/test_site_inventory.sh
sh tests/test_package_manifest.sh
git diff --check
```

Expected: 142 C tests and every existing Python, JavaScript, CLI, inventory, recorder, package, and diff check pass. No test opens a real elevator network socket.

- [ ] **Step 5: Update evidence status and commit**

Update the elevator row in `docs/mt8157-vendor-reference-matrix.md` to state that the 46-byte `08/02`, 42-byte `08/03`, bounded `08/83` parser, and pure two-attempt controller are implemented at the Doorfast-test evidence level. Preserve the explicit gaps for `08/82`/`08/83`现场 samples and physical elevator validation.

```bash
git add Makefile src/gvs_elevator_control.c src/gvs_elevator_control.h tests/test_gvs_elevator_control.c tests/test_main.c docs/mt8157-vendor-reference-matrix.md
git commit -m "feat: model bounded GVS elevator transactions"
```
