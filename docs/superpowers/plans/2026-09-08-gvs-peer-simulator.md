# GVS Offline Peer Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a deterministic, offline GVS peer simulator that drives Doorfast's production identity, presence, synchronization, and incoming-call receive paths without creating sockets or entering the APK.

**Architecture:** A test-only support library owns a fixed-capacity inbound-frame queue, synthetic peer identities, a monotonic clock, and action counters. It consumes `df_gvs_presence_action` callbacks, serializes only verified `91/*` and `03/01` frames with existing production serializers, and returns frames to callers for delivery through production receive APIs. A separate CLI runs three fixed scenarios and emits stable JSON Lines from public runtime snapshots.

**Tech Stack:** C17, existing Doorfast protocol modules, the repository's C test harness, POSIX shell CLI tests, Make.

**Spec:** `docs/superpowers/specs/2026-09-08-gvs-peer-simulator-design.md`

## Global Constraints

- The simulator is test-only and MUST NOT be compiled into `/usr/sbin/doorfast`, `doorfast-*.apk`, or `luci-app-doorfast-*.apk`.
- It MUST NOT create sockets, open pcap, select a network interface, or accept arbitrary frame data or real credentials.
- The 8-byte random and encryption fields MUST use fixed synthetic bytes only and MUST NOT appear in JSON Lines output.
- All synchronization replies MUST enter through `df_gvs_runtime_sync_receive()`; all calls MUST enter through `df_gvs_receive_datagram()`.
- No simulator function may directly mutate Doorfast presence, synchronization, or session fields.
- Unknown `0x07` peer-online reply semantics MUST remain an explicit evidence gap; synchronization participation MUST NOT increase `online_peers`.
- Logical time MUST be monotonic, queues MUST be fixed-capacity, and errors MUST preserve the pre-call simulator state.
- Local implementation commits may be created task by task, but push only once after final verification to avoid redundant 20-minute GitHub Actions builds; do not continuously poll the final run.

---

### Task 1: Fixed-capacity simulator core and synthetic call frames

**Files:**
- Create: `tests/support/gvs_peer_sim.h`
- Create: `tests/support/gvs_peer_sim.c`
- Create: `tests/test_gvs_peer_sim.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `df_gvs_control_serialize()`, `df_gvs_frame_parse()`, `enum df_gvs_presence_action_type`.
- Produces: the complete simulator API declared in the spec, with `DF_GVS_SIM_FRAME_CAPACITY` fixed at 16 frames and `df_gvs_peer_sim_next_frame()` returning `DF_ERR_IO` when the queue is empty.

- [ ] **Step 1: Declare two failing tests and register them**

Add these declarations and calls to `tests/test_main.c`, and change `test_suite_count()` plus its assertion from 56 to 58:

```c
void test_gvs_peer_sim_rejects_invalid_time_and_bounds_queue(void);
void test_gvs_peer_sim_builds_synthetic_call_frame(void);
```

Create `tests/test_gvs_peer_sim.c` with tests that:

```c
void test_gvs_peer_sim_rejects_invalid_time_and_bounds_queue(void) {
    const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
    struct df_gvs_peer_sim *sim = NULL;
    unsigned i;

    TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_create(
        &sim, DF_GVS_SIM_NO_PEER, local, 100));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_gvs_peer_sim_advance(sim, 99));
    for (i = 0; i < DF_GVS_SIM_FRAME_CAPACITY; ++i) {
        TEST_ASSERT_INT_EQ(DF_OK, df_gvs_peer_sim_make_call(sim, local));
    }
    TEST_ASSERT_INT_EQ(DF_ERR_IO, df_gvs_peer_sim_make_call(sim, local));
    df_gvs_peer_sim_destroy(sim);
}
```

The call-frame test must pop one frame, parse it with `df_gvs_frame_parse()`, and assert: exact destination, source type `0x32`, family `0x03`, opcode `0x01`, zero payload, and total length `DF_GVS_CONTROL_HEADER_SIZE`.

- [ ] **Step 2: Run the test to verify RED**

Run:

```sh
make -B test
```

Expected: compilation fails because `tests/support/gvs_peer_sim.h` and the simulator functions do not exist.

- [ ] **Step 3: Define the public test-support interface**

Create `tests/support/gvs_peer_sim.h` with include guards and these declarations:

```c
#define DF_GVS_SIM_FRAME_CAPACITY 16U

enum df_gvs_peer_sim_scenario {
    DF_GVS_SIM_NO_PEER = 0,
    DF_GVS_SIM_LOWER_PEER,
    DF_GVS_SIM_MAINTAINER_LOSS,
};

struct df_gvs_peer_sim;

int df_gvs_peer_sim_create(struct df_gvs_peer_sim **sim,
                           enum df_gvs_peer_sim_scenario scenario,
                           const uint8_t local[6], uint64_t now_ms);
void df_gvs_peer_sim_destroy(struct df_gvs_peer_sim *sim);
int df_gvs_peer_sim_emit(const struct df_gvs_presence_action *action,
                         void *context);
int df_gvs_peer_sim_advance(struct df_gvs_peer_sim *sim, uint64_t now_ms);
int df_gvs_peer_sim_next_frame(struct df_gvs_peer_sim *sim,
                               const uint8_t **frame, size_t *length);
int df_gvs_peer_sim_make_call(struct df_gvs_peer_sim *sim,
                              const uint8_t destination[6]);
size_t df_gvs_peer_sim_action_count(
    const struct df_gvs_peer_sim *sim,
    enum df_gvs_presence_action_type type);
```

- [ ] **Step 4: Implement allocation, monotonic time, queueing, and calls**

In `tests/support/gvs_peer_sim.c`, define a private frame slot with `uint8_t data[DF_GVS_SYNC_MAX_PACKET_SIZE]` and `size_t length`, then an opaque simulator containing exactly 16 slots, head/count indices, local/lower-peer/door identities, current time, scenario, action counters, and booleans needed by later tasks.

Use this field provider; do not expose its bytes in any output:

```c
static int sim_header_fields(uint8_t random_code[8],
                             uint8_t encryption_code[8], void *context) {
    (void)context;
    memset(random_code, 0x31, 8);
    memset(encryption_code, 0x41, 8);
    return DF_OK;
}
```

Initialize the lower peer by copying the local address and setting byte 5 to `0x01`. Initialize the door address as `{0x32, local[1], local[2], 0x00, local[4], 0x00}`. Validate the scenario range and identity with `df_gvs_identity_unicast_ip()` before allocating.

`df_gvs_peer_sim_make_call()` must serialize family `0x03`, opcode `0x01`, and no payload into a temporary slot, committing the queue count only after serialization succeeds. `next_frame()` returns the current head and removes it from the logical queue; the pointer remains valid until that slot is reused.

- [ ] **Step 5: Add the support source to the C tests and verify GREEN**

Add `tests/test_gvs_peer_sim.c tests/support/gvs_peer_sim.c` to `TEST_SOURCES` and `-Itests/support` to `CFLAGS` in `Makefile`.

Run:

```sh
make -B test
```

Expected: 58 tests, zero failures.

- [ ] **Step 6: Commit the simulator core locally**

```sh
git add Makefile tests/test_main.c tests/test_gvs_peer_sim.c tests/support/gvs_peer_sim.h tests/support/gvs_peer_sim.c
git commit -m "test: add fixed-capacity GVS peer simulator"
```

Do not push yet.

---

### Task 2: Drive synchronization election and maintainer takeover

**Files:**
- Modify: `tests/support/gvs_peer_sim.c`
- Modify: `tests/test_gvs_peer_sim.c`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: Task 1 simulator API; `df_gvs_runtime_sync_start()`, `df_gvs_runtime_sync_tick()`, `df_gvs_runtime_sync_receive()`, `df_gvs_runtime_sync_status()`.
- Produces: automatic `91/82` response for `DF_GVS_SIM_LOWER_PEER`; automatic `91/81` followed by one scheduled simulator-only single-item `91/03 Period` for `DF_GVS_SIM_MAINTAINER_LOSS`; stable per-action counters.

- [ ] **Step 1: Add three failing lifecycle tests**

Register these tests and change the suite count from 58 to 61:

```c
void test_gvs_peer_sim_elects_maintainer_without_peers(void);
void test_gvs_peer_sim_follows_lower_peer_sync_replies(void);
void test_gvs_peer_sim_takes_over_after_two_missed_periods(void);
```

In `tests/test_gvs_peer_sim.c`, add a helper that performs one deterministic step in this order:

```c
static int sim_step(struct df_gvs_peer_sim *sim,
                    struct df_gvs_runtime_sync *sync, uint64_t now_ms) {
    const uint8_t *frame;
    size_t length;
    struct df_gvs_runtime_sync_result result;

    if (df_gvs_peer_sim_advance(sim, now_ms) != DF_OK) return DF_ERR_INVALID;
    while (df_gvs_peer_sim_next_frame(sim, &frame, &length) == DF_OK) {
        if (df_gvs_runtime_sync_receive(sync, frame, length, now_ms,
                                        &result) != DF_OK) return DF_ERR_IO;
    }
    if (df_gvs_runtime_sync_tick(sync, now_ms, df_gvs_peer_sim_emit, sim) != DF_OK)
        return DF_ERR_IO;
    while (df_gvs_peer_sim_next_frame(sim, &frame, &length) == DF_OK) {
        if (df_gvs_runtime_sync_receive(sync, frame, length, now_ms,
                                        &result) != DF_OK) return DF_ERR_IO;
    }
    return DF_OK;
}
```

The no-peer test must step at `3000, 3500, 4000, 4500, 5500, 6500, 7500` ms and assert `phase=PERIODIC`, `role=MAINTAINER`, nine sync-ask actions, nine version-ask actions, and zero online peers. Each of the three rounds targets the three other `0x61` indoor extensions, so counting only rounds as actions would be incorrect.

The lower-peer test starts version 7, steps through 7500 ms, and asserts that a same-apartment byte-5 `0x01` peer's `91/82` makes local byte-5 `0x02` a follower while preserving version 7. It must also assert `online_peers=0` to freeze the `0x07` evidence boundary.

The maintainer-loss test must accept `91/81` at 3000 ms, accept one scheduled empty `91/03 Period` at 63000 ms, remain follower with one miss at 123000 ms, then become maintainer and emit periodic synchronization at 183000 ms.

- [ ] **Step 2: Run the focused suite to verify RED**

Run:

```sh
make -B test
```

Expected: the new lifecycle assertions fail because `df_gvs_peer_sim_emit()` only counts actions and does not yet queue synchronization replies.

- [ ] **Step 3: Implement action counting and verified replies**

For every action type less than or equal to `DF_GVS_PRESENCE_PERIODIC_SYNC`, increment its counter after validating the action and context. Never synthesize a reply to `PEER_PROBE`. For an action that requires a reply, serialize into an uncommitted slot first, then commit the queue and action counter together so a full queue or serialization error leaves the simulator unchanged.

For `DF_GVS_SIM_LOWER_PEER`, on the first `SYNC_VERSION_ASK` targeting the lower peer, queue:

```c
uint8_t payload[2] = {
    (uint8_t)(peer_version & 0xffU),
    (uint8_t)(peer_version >> 8U),
};
df_gvs_control_serialize(slot, capacity, &length,
                         sim->local, sim->lower_peer,
                         0x91, 0x82, payload, 2,
                         sim_header_fields, NULL);
```

For `DF_GVS_SIM_MAINTAINER_LOSS`, on the first `SYNC_ASK_ACTION`, queue the same two-byte version payload with opcode `0x81`, then schedule one peer Period frame for `sim->now_ms + 60000U`. When `advance()` reaches the scheduled time, initialize a simulator-only `df_gvs_sync_store`, register `"sim_state"="present"`, and use `df_gvs_sync_periodic_serialize()` to queue one `91/03` frame from the lower peer to local; mark it sent only after queueing succeeds. The local Doorfast store will ignore the unregistered value while still exercising Period timing and arbitration.

An action aimed at another valid candidate is counted but does not receive a reply. If the queue is full or serialization fails for the lower peer, return an error without incrementing the counter or reply/sent flags. `advance()` must perform scheduled Period generation on a local copy of the simulator and commit the new time, queue, and sent flag together.

- [ ] **Step 4: Verify lifecycle GREEN and all earlier tests**

Run:

```sh
make -B test
```

Expected: 61 tests, zero failures; `online_peers` remains zero in the two peer scenarios.

- [ ] **Step 5: Commit lifecycle simulation locally**

```sh
git add tests/support/gvs_peer_sim.c tests/test_gvs_peer_sim.c tests/test_main.c
git commit -m "test: simulate GVS election and maintainer loss"
```

Do not push yet.

---

### Task 3: Verify door-station target selection and malformed input

**Files:**
- Modify: `tests/test_gvs_peer_sim.c`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `df_gvs_peer_sim_make_call()`, `df_gvs_peer_sim_next_frame()`, `df_gvs_receive_datagram()`.
- Produces: regression coverage for same-apartment first-five-byte routing and strict 42-byte public-header validation.

- [ ] **Step 1: Add two call-routing characterization tests**

Register these functions and change the suite count from 61 to 63:

```c
void test_gvs_peer_sim_routes_calls_by_apartment_scope(void);
void test_gvs_peer_sim_rejects_malformed_and_non_call_frames(void);
```

The routing test must use fresh session/deadline objects for each case:

```c
const uint8_t local[6] = {0x61, 2, 1, 1, 1, 2};
const uint8_t same_apartment_other_extension[6] = {0x61, 2, 1, 1, 1, 4};
const uint8_t other_apartment[6] = {0x61, 2, 1, 1, 2, 2};
```

Calls to `local` and `same_apartment_other_extension` must return `accepted_call=true`, one `INCOMING_CALL` transition, and session `DF_GVS_RINGING`. A call to `other_apartment` must return `accepted_call=false` and leave the session `DF_GVS_IDLE`.

The malformed-input test must copy a valid simulator frame into a local buffer and independently verify rejection of:

- length 41;
- byte 0 changed from `G`;
- declared payload length changed from 0 to 1 without adding a byte;
- opcode changed from `0x01` to `0x50`.

Malformed parse errors may return `DF_ERR_INVALID`; a structurally valid non-call must return `DF_OK` with `accepted_call=false`. None may change the session.

- [ ] **Step 2: Run the characterization tests against current production semantics**

Run:

```sh
make -B test
```

Expected: the new tests compile and pass because current `df_gvs_frame_is_for_identity()` already implements the documented first-five-byte rule. If they fail, stop and diagnose the discrepancy; do not change the expected rule merely to obtain GREEN.

- [ ] **Step 3: Correct only discrepancies demonstrated by the characterization tests**

Do not change production target matching if the existing `df_gvs_frame_is_for_identity()` first-five-byte behavior already passes. Fix only simulator call construction or test isolation defects revealed by RED. A production change is allowed only if a failing test demonstrates divergence from `docs/gvs-identity-addressing.md`; if needed, keep matching exactly:

```c
return memcmp(frame->destination, identity, 5) == 0;
```

- [ ] **Step 4: Verify call routing GREEN**

Run:

```sh
make -B test
```

Expected: 63 tests, zero failures.

- [ ] **Step 5: Commit call targeting locally**

```sh
git add tests/test_gvs_peer_sim.c tests/test_main.c tests/support/gvs_peer_sim.c src/gvs_identity.c
git commit -m "test: verify GVS apartment call targeting"
```

If `src/gvs_identity.c` did not change, omit it from `git add`.

Do not push yet.

---

### Task 4: Add the deterministic JSON Lines scenario CLI

**Files:**
- Create: `tools/gvs-peer-sim.c`
- Create: `tests/test_gvs_peer_sim_cli.sh`
- Modify: `Makefile`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: Task 1 simulator API, Task 2 deterministic stepping pattern, runtime status naming functions, and Task 3 call routing.
- Produces: `make peer-sim` → `build/gvs-peer-sim`; strict `--scenario no-peer|lower-peer|maintainer-loss` interface; stable JSON Lines output.

- [ ] **Step 1: Write the failing CLI test**

Create executable `tests/test_gvs_peer_sim_cli.sh` that runs:

```sh
set -eu
make peer-sim >/dev/null

build/gvs-peer-sim --scenario no-peer > build/no-peer.jsonl
build/gvs-peer-sim --scenario no-peer > build/no-peer-repeat.jsonl
cmp build/no-peer.jsonl build/no-peer-repeat.jsonl
grep -F '"event":"election_complete","phase":"periodic","role":"maintainer"' build/no-peer.jsonl
grep -F '"destination_scope":"same_apartment","accepted_call":true,"session_state":"ringing"' build/no-peer.jsonl
grep -F '"destination_scope":"other_apartment","accepted_call":false,"session_state":"idle"' build/no-peer.jsonl

build/gvs-peer-sim --scenario lower-peer > build/lower-peer.jsonl
grep -F '"role":"follower"' build/lower-peer.jsonl
grep -F '"online_peers":0,"peer_presence_evidence_gap":true' build/lower-peer.jsonl

build/gvs-peer-sim --scenario maintainer-loss > build/maintainer-loss.jsonl
grep -F '"event":"first_period_missed","role":"follower"' build/maintainer-loss.jsonl
grep -F '"event":"maintainer_takeover","role":"maintainer"' build/maintainer-loss.jsonl

! grep -E '31313131|41414141|random|encrypt|secret|credential' build/*.jsonl

if build/gvs-peer-sim --scenario unknown >/dev/null 2>&1; then exit 1; else
    test "$?" -eq 2
fi
```

Add a package-manifest assertion that neither package recipe references the simulator:

```sh
! grep -R -q 'gvs-peer-sim\|gvs_peer_sim' package/doorfast package/luci-app-doorfast scripts/prepare-sdk-package.sh
```

- [ ] **Step 2: Run the CLI test to verify RED**

Run:

```sh
sh tests/test_gvs_peer_sim_cli.sh
```

Expected: `make` fails because the `peer-sim` target and tool source do not exist.

- [ ] **Step 3: Add a focused simulator build target**

In `Makefile`, define `SIM_SOURCES` and the target with only:

```make
SIM_SOURCES := tools/gvs-peer-sim.c tests/support/gvs_peer_sim.c \
	src/event.c src/gvs_deadline.c src/gvs_frame.c src/gvs_identity.c \
	src/gvs_observer.c src/gvs_presence.c src/gvs_priority.c \
	src/gvs_receive.c src/gvs_runtime_sync.c src/gvs_serialize.c \
	src/gvs_session.c src/gvs_sync.c src/gvs_sync_adapters.c

peer-sim: build/gvs-peer-sim

build/gvs-peer-sim: $(SIM_SOURCES) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SIM_SOURCES) -o $@
```

Add `peer-sim` to `.PHONY` and compile `build/gvs-peer-sim` with `$(CC) $(CPPFLAGS) $(CFLAGS) $(SIM_SOURCES) -o $@`; do not link pcap because no simulator source uses it.

- [ ] **Step 4: Implement strict scenario execution and JSON Lines**

In `tools/gvs-peer-sim.c`:

- accept exactly three arguments: executable, `--scenario`, scenario name;
- return 2 for any other form;
- use local identity `{0x61, 2, 1, 1, 1, 2}` and initial sync version 7;
- use the same drain-before-tick, tick, drain-after-tick ordering as Task 2;
- run no-peer and lower-peer through 7500 ms;
- run maintainer-loss through 63000, 123000, and 183000 ms;
- query `df_gvs_runtime_sync_status()` at fixed milestones;
- emit booleans as JSON `true`/`false`, never quoted;
- run the three call-target cases with fresh session/deadline state and emit the required scope records;
- use a local exhaustive session-name function returning `idle`, `preview`, `ringing`, `talking`, or `ended` and fail on unknown values.

The no-peer final line must be structurally equivalent to:

```json
{"time_ms":7500,"event":"election_complete","phase":"periodic","role":"maintainer","version":7,"online_peers":0,"peer_presence_evidence_gap":false}
```

The lower-peer final line uses `role=follower` and `peer_presence_evidence_gap=true`. The maintainer-loss scenario emits separate `first_period_missed` and `maintainer_takeover` records.

- [ ] **Step 5: Verify CLI GREEN and APK exclusion**

Run:

```sh
sh tests/test_gvs_peer_sim_cli.sh
sh tests/test_package_manifest.sh
```

Expected: both scripts exit 0; repeated no-peer output is byte-identical; package recipes contain no simulator reference.

- [ ] **Step 6: Commit the CLI locally**

```sh
git add Makefile tools/gvs-peer-sim.c tests/test_gvs_peer_sim_cli.sh tests/test_package_manifest.sh
git commit -m "test: add deterministic GVS simulator scenarios"
```

Do not push yet.

---

### Task 5: Document evidence, run memory safety checks, and publish once

**Files:**
- Modify: `docs/superpowers/specs/2026-09-08-gvs-peer-simulator-design.md`
- Modify: `docs/doorfast-host-mode-roadmap.md`
- Modify: `docs/2026-09-08_reverse-gvs-sync-report.md`

**Interfaces:**
- Consumes: all simulator scenarios, local test suite, CLI output, and artifact exclusion check.
- Produces: an Evidence → Finding → Path record that distinguishes simulated presence maintenance from real door-station acceptance.

- [ ] **Step 1: Run the complete normal regression**

```sh
make -B test doorfast
sh tests/test_main_cli.sh
sh tests/test_gvs_peer_sim_cli.sh
sh tests/test_package_manifest.sh
node tests/test_luci_status.js
python3 -B -m unittest discover -s tests -p 'test_*.py'
```

Expected: 63 C tests and all shell, JavaScript, and Python tests pass.

- [ ] **Step 2: Run AddressSanitizer and UndefinedBehaviorSanitizer**

```sh
make clean
make test CC=clang CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support -I/opt/homebrew/opt/libpcap/include -fsanitize=address,undefined -fno-omit-frame-pointer'
make clean
make -B test doorfast
```

Expected: sanitizer test process exits 0 with no AddressSanitizer or UndefinedBehaviorSanitizer diagnostics; the final normal build restores `build/doorfast-tests` and `build/doorfast`.

- [ ] **Step 3: Record exact simulator evidence**

Run:

```sh
sha256sum tests/support/gvs_peer_sim.c tools/gvs-peer-sim.c tests/test_gvs_peer_sim.c tests/test_gvs_peer_sim_cli.sh
build/gvs-peer-sim --scenario no-peer
build/gvs-peer-sim --scenario lower-peer
build/gvs-peer-sim --scenario maintainer-loss
```

Append the emitted SHA-256 values and redacted scenario excerpts as `E-009` in `docs/2026-09-08_reverse-gvs-sync-report.md`. Add validated `F-008` referencing both implementation/test evidence and the earlier static `E-001`/presence evidence. Add `Path P-003` with these steps:

1. configured identity starts the production presence state machine;
2. structured actions enter the simulator callback;
3. verified synthetic frames return through production receive APIs;
4. public status snapshots and session state produce redacted JSON Lines;
5. absent `0x07` evidence keeps `online_peers=0`.

Set the residual risks to real public-header compatibility, unknown `0x07` response semantics, real UDP delivery, and door-station acceptance.

- [ ] **Step 4: Update project status without overstating M2**

In the design spec, change status to `implemented and locally verified`. In the roadmap, mark the local protocol-peer test environment sub-item complete, but keep M2 real-device and active-send items unchecked. Add a progress sentence stating that election, takeover, and call targeting are simulated through production APIs while actual peer-online reply parsing and device acceptance remain open.

- [ ] **Step 5: Verify documentation and repository state**

```sh
git diff --check
! rg -n 'TODO|TBD|PLACEHOLDER|真实设备已接受|主机模式已完成' \
  docs/superpowers/specs/2026-09-08-gvs-peer-simulator-design.md \
  docs/doorfast-host-mode-roadmap.md \
  docs/2026-09-08_reverse-gvs-sync-report.md
git status --short
```

Expected: no whitespace errors, prohibited overclaims, generated binaries, JSON Lines, or Python cache files in the commit set.

- [ ] **Step 6: Commit documentation, inspect the full branch diff, and push once**

```sh
git add docs/superpowers/specs/2026-09-08-gvs-peer-simulator-design.md \
  docs/doorfast-host-mode-roadmap.md \
  docs/2026-09-08_reverse-gvs-sync-report.md
git commit -m "docs: record offline GVS simulator evidence"
git diff --check origin/main...HEAD
git status --short --branch
git push origin feature/transparent-foundation
```

Query the resulting GitHub Actions run once to record its URL and initial status. Do not poll it; wait for the user to report completion before downloading any new artifacts.
