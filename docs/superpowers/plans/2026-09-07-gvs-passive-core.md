# GVS passive core implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a testable, passive-only GVS frame parser and session engine to
Doorfast, configured with an explicit administrator-selected GVS interface.

**Architecture:** Keep protocol parsing separate from capture and configuration.
`gvs_frame` validates a bounded UDP payload and maps only evidenced message
families to normalized events; `gvs_session` enforces lifecycle transitions
without sending packets. The capture module supplies a GVS-only BPF filter,
and configuration validates arbitrary administrator-supplied interface names
without probing or changing network state.

**Tech Stack:** C17, libpcap, existing project test harness, ImmortalWrt
25.12.1 x86_64 SDK.

**Spec:** `docs/superpowers/specs/2026-09-07-doorfast-gvs-design.md`

## Global constraints

- Target x86_64 ImmortalWrt 25.12.1; retain C17 and no new dependencies.
- GVS processing is passive only: this plan must not create any UDP send,
  connect, replay, control, unlock, hangup, or elevator function.
- `gvs_interface` is an explicit arbitrary local interface name; never assume
  `eth0`, `wlan0`, a bridge, or a default route.
- Do not create, modify, or delete interfaces, routes, addresses, VLANs,
  bridges, firewall rules, or DNS configuration.
- Fixtures must be synthetic: use invented six-byte addresses and omit real
  IP addresses, device IDs, dynamic session material, audio, and video.
- Preserve legacy import only as a disabled historical-configuration exporter:
  it must set `enabled` to false, must not enable GVS runtime capture, and
  must not make Dnake a supported protocol.
- Run tests with `make -B test`, because the current Make target executes the
  binary only when recompilation occurs.

---

## File structure

| Path | Responsibility |
|---|---|
| `src/config.h`, `src/config.c` | Add and validate GVS-specific selected-interface fields without interface discovery or mutation. |
| `src/event.h`, `src/event.c` | Extend normalized events for passive GVS observations. |
| `src/gvs_frame.h`, `src/gvs_frame.c` | Validate the GVS envelope and map safe message-family bytes to events. |
| `src/gvs_session.h`, `src/gvs_session.c` | Apply only valid passive lifecycle transitions for one bounded GVS session. |
| `src/capture.c` | Supply the exact passive GVS UDP BPF filter. |
| `package/doorfast/files/doorfast.config` | Ship disabled GVS configuration with no predefined interface. |
| `tests/test_config.c`, `tests/test_capture.c`, `tests/test_main.c` | Cover configuration and filter changes. |
| `tests/test_gvs_frame.c`, `tests/test_gvs_session.c` | Exercise synthetic frames and lifecycle rules. |
| `Makefile` | Include the GVS sources and test translation units. |
| `README.md` | Document the implemented passive GVS boundary and interface configuration. |

## Task 1: Add explicit GVS configuration fields

**Files:**
- Modify: `src/config.h`
- Modify: `src/config.c`
- Modify: `src/main.c`
- Modify: `tests/test_config.c`
- Modify: `tests/test_main.c`
- Modify: `tests/test_main_cli.sh`

**Interfaces:**
- Produces `struct df_config` fields `const char *gvs_interface`,
  `const char *uplink_interface`, and `bool passive_only`.
- Produces `int df_config_validate(const struct df_config *config)` accepting
  enabled GVS configurations only when `gvs_interface` is nonempty and
  `passive_only` is true.
- Preserves `doorfast --import-legacy` only as an explicitly disabled historical
  draft export; it cannot create an enabled runtime configuration.
- Reserved for a daemon configuration loader that is deliberately outside this
  passive-core plan.

- [ ] **Step 1: Write failing configuration tests**

  In `tests/test_config.c`, change the existing enabled configuration in
  `test_config_validation` from `dnake` to a valid GVS configuration with
  `gvs_interface = "br-door"` and `passive_only = true`. Add a new test with
  a nonstandard VLAN-like name, a valid optional uplink, a missing GVS
  interface, and a non-passive request:

  ```c
  void test_gvs_config_requires_explicit_passive_interface(void) {
      struct df_config valid = {
          .enabled = true,
          .brand = "gvs",
          .gvs_interface = "vlan-door.42",
          .uplink_interface = "bond-home",
          .passive_only = true,
      };
      struct df_config missing_interface = valid;
      struct df_config active_request = valid;

      missing_interface.gvs_interface = "";
      active_request.passive_only = false;
      TEST_ASSERT_INT_EQ(DF_OK, df_config_validate(&valid));
      TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&missing_interface));
      TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&active_request));
  }
  ```

  Declare and call the test from `tests/test_main.c`; increment its expected
  suite count.

- [ ] **Step 2: Run the focused suite to verify it fails**

  Run: `make -B test`

  Expected: FAIL because `gvs_interface`, `uplink_interface`, and
  `passive_only` do not exist in `struct df_config`.

- [ ] **Step 3: Implement the minimal configuration change**

  Add the three fields to `struct df_config`. In `df_config_validate`, retain
  the existing disabled-service shortcut. For enabled configurations, accept
  exactly `brand == "gvs"`, require a nonempty `gvs_interface`, require
  `passive_only == true`, and retain existing delay-range validation. Do not
  call `if_nametoindex`, libuci, shell commands, or any network API.

  In `df_config_import_legacy`, parse legacy fields only to produce a reviewable
  historical draft, then force `imported->config.enabled = false` before
  calling `df_config_validate`. Update the existing legacy-import test to
  expect a disabled result. In `src/main.c`, replace the Dnake-specific import
  error with `doorfast: legacy configuration is not supported`, and in
  `tests/test_main_cli.sh` assert that a successful import prints
  `option enabled '0'`. The importer may retain the legacy brand text for
  review, but it must not yield an enabled non-GVS configuration.

- [ ] **Step 4: Run the focused suite to verify it passes**

  Run: `make -B test`

  Expected: exit code 0.

- [ ] **Step 5: Commit the configuration slice**

  ```sh
  git add src/config.h src/config.c src/main.c tests/test_config.c \
    tests/test_main.c tests/test_main_cli.sh
  git commit -m "feat: require explicit passive GVS interface"
  ```

## Task 2: Define passive GVS events and synthetic envelopes

**Files:**
- Modify: `src/event.h`
- Modify: `src/event.c`
- Create: `src/gvs_frame.h`
- Create: `src/gvs_frame.c`
- Create: `tests/test_gvs_frame.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

**Interfaces:**
- Produces event types `DF_EVENT_STATION_OBSERVED`,
  `DF_EVENT_PREVIEW_STARTED`, `DF_EVENT_INCOMING_CALL`,
  `DF_EVENT_SESSION_ESTABLISHED`, `DF_EVENT_UNLOCK_RESULT_OBSERVED`, and
  `DF_EVENT_HANGUP`.
- Produces `struct df_gvs_frame { uint8_t destination[6]; uint8_t source[6];
  uint8_t family; uint8_t opcode; uint8_t status; }`.
- Produces `int df_gvs_frame_parse(const uint8_t *data, size_t length,
  struct df_gvs_frame *frame, struct df_event *event)`.
- Consumed by Task 3; returns `DF_ERR_INVALID` for malformed envelope and
  `DF_OK` with `DF_EVENT_UNKNOWN` for a syntactically valid unsupported family.

- [ ] **Step 1: Write failing synthetic frame tests**

  Use a local test helper that builds no packet capable of targeting a real
  system. The fixed payload layout is 6-byte ASCII `GVSGVS`, four `0xA5` bytes,
  invented destination and source addresses, 16 zeroed opaque bytes, then
  family, opcode, and status. The opaque region models the observed
  per-session area but is never interpreted.

  ```c
  static size_t make_frame(uint8_t *frame, uint8_t family, uint8_t opcode,
                           uint8_t status) {
      static const uint8_t prefix[] = {'G','V','S','G','V','S',
                                       0xA5,0xA5,0xA5,0xA5};
      static const uint8_t destination[] = {0x32,0x00,0x00,0x00,0x00,0x02};
      static const uint8_t source[] = {0x61,0x00,0x00,0x00,0x00,0x01};
      memcpy(frame, prefix, sizeof(prefix));
      memcpy(frame + 10, destination, sizeof(destination));
      memcpy(frame + 16, source, sizeof(source));
      memset(frame + 22, 0, 16);
      frame[38] = family;
      frame[39] = opcode;
      frame[40] = status;
      return 41;
  }

  void test_gvs_preview_frame_maps_to_event(void) {
      uint8_t packet[41];
      struct df_gvs_frame frame = {0};
      struct df_event event = {0};

      TEST_ASSERT_INT_EQ(DF_OK, df_gvs_frame_parse(
          packet, make_frame(packet, 0x03, 0x04, 0x00), &frame, &event));
      TEST_ASSERT_INT_EQ(DF_EVENT_PREVIEW_STARTED, event.type);
  }
  ```

  Add tests rejecting a short frame and a bad marker, and accepting a valid
  unknown opcode as `DF_EVENT_UNKNOWN`.

- [ ] **Step 2: Run the focused suite to verify it fails**

  Run: `make -B test`

  Expected: FAIL because `gvs_frame.h` and `df_gvs_frame_parse` do not exist.

- [ ] **Step 3: Implement bounded frame validation**

  In `src/gvs_frame.c`, require a length of at least 41, compare the magic and
  marker using fixed-size `memcmp`, copy exactly twelve address bytes only
  after validation, skip the sixteen opaque bytes without reading them, and map
  the family, opcode, and status at offsets 38, 39, and 40 only to these safe
  observation values:

  ```c
  family 0x03, opcode 0x04 -> DF_EVENT_PREVIEW_STARTED
  family 0x03, opcode 0x86 -> DF_EVENT_STATION_OBSERVED
  family 0x03, opcode 0x50 -> DF_EVENT_SESSION_ESTABLISHED
  family 0x04, opcode 0x89 -> DF_EVENT_UNLOCK_RESULT_OBSERVED
  ```

  Initialize every output before parsing. Unsupported but syntactically valid
  messages return `DF_OK` and `DF_EVENT_UNKNOWN`. Do not parse dynamic bytes,
  do not derive credentials, and do not construct or send a response.

  In `src/event.c`, return the exact names `StationObserved`, `PreviewStarted`,
  `SessionEstablished`, and `UnlockResultObserved` for the newly added event
  values. Retain the existing names for incoming calls, hangups, and unknown
  events.

- [ ] **Step 4: Run the focused suite to verify it passes**

  Run: `make -B test`

  Expected: exit code 0 with all existing SIP tests still passing.

- [ ] **Step 5: Commit the frame-parser slice**

  ```sh
  git add Makefile src/event.h src/event.c src/gvs_frame.h src/gvs_frame.c \
    tests/test_gvs_frame.c tests/test_main.c
  git commit -m "feat: parse passive GVS frame envelopes"
  ```

## Task 3: Add a passive GVS lifecycle state machine

**Files:**
- Create: `src/gvs_session.h`
- Create: `src/gvs_session.c`
- Create: `tests/test_gvs_session.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

**Interfaces:**
- Produces `enum df_gvs_session_state { DF_GVS_IDLE, DF_GVS_PREVIEW,
  DF_GVS_RINGING, DF_GVS_TALKING, DF_GVS_ENDED }`.
- Produces `struct df_gvs_session { enum df_gvs_session_state state;
  uint8_t peer[6]; }`.
- Produces `int df_gvs_session_apply(struct df_gvs_session *session,
  const struct df_gvs_frame *frame, const struct df_event *event)`.
- Consumes Task 2 events; never exposes an action or transport interface.

- [ ] **Step 1: Write failing lifecycle tests**

  Test a valid preview-to-talking-to-hangup sequence, an incoming-call path,
  and rejection of a peer address change while a session is active:

  ```c
  void test_gvs_session_tracks_passive_lifecycle(void) {
      struct df_gvs_session session = {0};
      struct df_gvs_frame frame = {.source = {0x32,0,0,0,0,2}};
      struct df_event preview = {.type = DF_EVENT_PREVIEW_STARTED};
      struct df_event talking = {.type = DF_EVENT_SESSION_ESTABLISHED};
      struct df_event hangup = {.type = DF_EVENT_HANGUP};

      TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &preview));
      TEST_ASSERT_INT_EQ(DF_GVS_PREVIEW, session.state);
      TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &talking));
      TEST_ASSERT_INT_EQ(DF_GVS_TALKING, session.state);
      TEST_ASSERT_INT_EQ(DF_OK, df_gvs_session_apply(&session, &frame, &hangup));
      TEST_ASSERT_INT_EQ(DF_GVS_ENDED, session.state);
  }
  ```

- [ ] **Step 2: Run the focused suite to verify it fails**

  Run: `make -B test`

  Expected: FAIL because `gvs_session.h` and `df_gvs_session_apply` do not
  exist.

- [ ] **Step 3: Implement only observation transitions**

  Permit `IDLE -> PREVIEW` on `DF_EVENT_PREVIEW_STARTED`,
  `IDLE -> RINGING` on `DF_EVENT_INCOMING_CALL`, `PREVIEW|RINGING -> TALKING`
  on `DF_EVENT_SESSION_ESTABLISHED`, and `PREVIEW|RINGING|TALKING -> ENDED`
  on `DF_EVENT_HANGUP`. `DF_EVENT_UNLOCK_RESULT_OBSERVED` may update no state
  and must return `DF_OK` only when the source peer matches the active session.
  Reject other transitions and any active-session peer mismatch with
  `DF_ERR_INVALID`. Copy only the six-byte source address, and do not add
  timers, packet emission, or retries.

- [ ] **Step 4: Run the focused suite to verify it passes**

  Run: `make -B test`

  Expected: exit code 0.

- [ ] **Step 5: Commit the session slice**

  ```sh
  git add Makefile src/gvs_session.h src/gvs_session.c tests/test_gvs_session.c \
    tests/test_main.c
  git commit -m "feat: track passive GVS session lifecycle"
  ```

## Task 4: Restrict capture configuration to GVS ports

**Files:**
- Modify: `src/capture.c`
- Modify: `tests/test_capture.c`

**Interfaces:**
- Changes `const char *df_capture_default_filter(void)` to return a
  passive-only GVS BPF expression.
- Produces no packet dispatch callback and no transport capability.

- [ ] **Step 1: Write the failing BPF filter test**

  Replace the expected filter with:

  ```c
  TEST_ASSERT_INT_EQ(0, strcmp(
      "udp and (port 8300 or port 8302 or port 8303 or port 8304)",
      df_capture_default_filter()));
  ```

- [ ] **Step 2: Run the focused suite to verify it fails**

  Run: `make -B test`

  Expected: FAIL because the current filter still targets SIP port 5060.

- [ ] **Step 3: Implement the exact GVS filter**

  Return exactly:

  ```c
  "udp and (port 8300 or port 8302 or port 8303 or port 8304)"
  ```

  Do not change `df_capture_open`; daemon capture wiring is deliberately
  outside this passive-core plan.

- [ ] **Step 4: Run the focused suite to verify it passes**

  Run: `make -B test`

  Expected: exit code 0.

- [ ] **Step 5: Commit the filter slice**

  ```sh
  git add src/capture.c tests/test_capture.c
  git commit -m "feat: restrict passive capture to GVS ports"
  ```

## Task 5: Ship disabled GVS package defaults and truthful documentation

**Files:**
- Modify: `package/doorfast/files/doorfast.config`
- Modify: `README.md`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Produces a default UCI `config gvs 'main'` with `enabled '0'`, empty
  `gvs_interface`, empty `uplink_interface`, `passive_only '1'`, and
  `capture_promiscuous '0'`.
- Preserves `doorfast.init` behavior: it does not start a disabled service.

- [ ] **Step 1: Write the failing package manifest assertion**

  In `tests/test_package_manifest.sh`, add exact checks:

  ```sh
  grep -F "config gvs 'main'" package/doorfast/files/doorfast.config
  grep -F "option enabled '0'" package/doorfast/files/doorfast.config
  grep -F "option gvs_interface ''" package/doorfast/files/doorfast.config
  grep -F "option uplink_interface ''" package/doorfast/files/doorfast.config
  grep -F "option passive_only '1'" package/doorfast/files/doorfast.config
  ```

- [ ] **Step 2: Run the manifest test to verify it fails**

  Run: `sh tests/test_package_manifest.sh`

  Expected: nonzero exit because the package configuration still uses the
  legacy `settings`/`dnake` fields.

- [ ] **Step 3: Update the package default and README**

  Replace the package configuration with only the fields listed above. In the
  README, show the same example and state that empty interface values keep the
  service disabled. Do not add LuCI, integration, device discovery, or update
  controls in this task.

- [ ] **Step 4: Run package and host verification**

  Run: `make -B test doorfast && sh tests/test_main_cli.sh && sh tests/test_package_manifest.sh && git diff --check`

  Expected: exit code 0.

- [ ] **Step 5: Commit the package-default slice**

  ```sh
  git add README.md package/doorfast/files/doorfast.config tests/test_package_manifest.sh
  git commit -m "build: ship disabled passive GVS defaults"
  ```

## Task 6: Verify the completed passive core and request review

**Files:**
- Modify: `README.md` only if verification exposes a documentation mismatch.

- [ ] **Step 1: Run the complete host verification**

  Run: `make -B test doorfast && sh tests/test_main_cli.sh && sh tests/test_package_manifest.sh && git diff --check && git status --short`

  Expected: every test exits 0, the diff check is silent, and status contains
  no uncommitted implementation files.

- [ ] **Step 2: Run a clean SDK package build through the existing CI workflow**

  Push the completed branch, then inspect the `host-tests` and `apk` jobs in
  `.github/workflows/build-apk.yml` for successful completion. Do not publish
  a release or install the APK on a router as part of this plan.

- [ ] **Step 3: Inspect the generated APK contents**

  Download the CI artifact and confirm it contains only the Doorfast APK and
  no PCAP fixture, media, private evidence, or credentials. Record the APK
  SHA-256 in the release-preparation notes only after the artifact is verified.

- [ ] **Step 4: Request code review**

  Present the verified commit range, test commands, and the explicit statement
  that no outbound GVS packet path exists. Do not merge, publish, or enable
  the service without separate user direction.
