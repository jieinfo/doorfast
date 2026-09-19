# Station Discovery and LuCI Implementation Plan

> **For AI agent workers:** Required sub-skill: use
> `subagent-driven-development` (recommended) or `executing-plans` to implement
> this plan task by task. Track progress with the checkboxes below.

**Goal:** Add multicast override, dynamic configured stations, verified `07/06`
discovery, a scan/adopt LuCI workflow, and dedicated read-only status and media
pages without changing the media ABI yet.

**Architecture:** A new station registry parses named UCI `station` sections
and owns stable station IDs. A separate discovery state machine emits one
three-frame broadcast burst and stores validated `07/86` candidates in a
64-entry LRU cache. Runtime ubus exposes configured stations and candidates;
LuCI writes only approved station sections.

**Technical stack:** C17, libpcap, libubus/blobmsg, OpenWrt UCI text format,
POSIX UDP sockets, LuCI JavaScript, Node.js tests, shell package tests.

**Spec:**
`docs/superpowers/specs/2026-09-18-multi-station-media-design.md`

## Global Constraints

- Active station discovery runs only in host mode.
- A scan sends `07/06` to `255.255.255.255:8300` at 0, 500, and 1000 ms.
- The logical destination is `32:<building>:<unit>:FF:FF:FF`; payload is
  `02 00 00 01`.
- Only matching `07/86` replies from valid `0x32` station identities enter the
  candidate cache.
- Discovery candidates never become configured stations automatically.
- Configured station count is dynamically allocated; the candidate cache is a
  64-entry LRU.
- A discovered route expires after 60 seconds; a configured fixed IPv4 does
  not expire.
- Multicast mode is `auto` or `custom`; a custom address must be IPv4
  multicast in `224.0.0.0/4`; UDP port stays `8300`.
- The status page is read-only. Door controls remain in Home Assistant.
- No API may describe a discovery reply as registration or physical action.
- PR titles and commits are in English.

---

## File Structure

**Create:**

- `src/station_registry.h`: immutable station IDs, station records, validation,
  legacy-singleton conversion, and dynamic registry ownership.
- `src/station_registry.c`: UCI station-section parsing, duplicate checks, route
  policy, lookup, and serialization helpers.
- `src/gvs_station_discovery.h`: scan scheduler and 64-entry candidate-cache
  types.
- `src/gvs_station_discovery.c`: `07/06` schedule, strict `07/86` admission,
  LRU updates, and candidate snapshots.
- `tests/test_station_registry.c`: registry parsing, validation, duplicate, and
  legacy migration tests.
- `tests/test_gvs_station_discovery.c`: scan timing, frame intent, filtering,
  LRU, and route freshness tests.
- `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/media.js`:
  global media and credential form.
- `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/stations.js`:
  configured station grid, candidate table, scan and adopt actions.
- `tests/test_luci_media.js`: dedicated media-page behavior.
- `tests/test_luci_stations.js`: station validation and scan/adopt behavior.
- `tests/test_media_credential_migration.sh`: removal of the legacy media relay
  token without changing the RTSP password.

**Modify:**

- `src/runtime_config.h`, `src/runtime_config.c`: multicast settings, station
  registry ownership, legacy station mapping, and capacity parsing without a
  fixed limit of four.
- `src/gvs_multicast.h`, `src/gvs_multicast.c`: accept an effective group chosen
  by runtime configuration.
- `src/gvs_udp_sender.h`, `src/gvs_udp_sender.c`: enable broadcast and emit a
  serialized station-discovery frame.
- `src/runtime_service.h`, `src/runtime_service.c`: tick discovery, observe
  replies, update configured routes, and publish station events.
- `src/runtime_ubus.h`, `src/runtime_ubus.c`: station list, candidate list, and
  scan methods.
- `package/doorfast/files/doorfast-http.sh`: read-only station-list endpoint.
- `src/media_credentials.h`, `src/media_credentials.c`: keep only the RTSP
  secret.
- `src/media_module.h`, `src/media_module.c`, `src/runtime_media_module.c`:
  remove the separate media relay while preserving ABI v2 for this plan.
- `src/media_relay.h`, `src/media_relay.c`: delete after all callers and tests
  are removed.
- `package/doorfast/files/doorfast.config`: new multicast defaults and explicit
  station section migration target.
- `package/doorfast/Makefile`, `package/doorfast-media/Makefile`: install new
  sources and run credential migration.
- `package/luci-app-doorfast/Makefile`: install `media.js` and `stations.js`.
- `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js`:
  remove every editable form.
- `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/deployment.js`:
  auto/custom multicast controls.
- `package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json`:
  add station and media pages with distinct ordering.
- `package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json`:
  grant the exact new ubus methods.
- `tests/test_main.c`, `Makefile`: compile and invoke new C suites.
- `tests/test_runtime_config.c`, `tests/test_gvs_multicast.c`,
  `tests/test_gvs_udp_sender.c`, `tests/test_runtime_service.c`,
  `tests/test_runtime_ubus.c`, `tests/test_doorfast_http.sh`,
  `tests/test_package_manifest.sh`, `tests/test_luci_status.js`,
  `tests/test_luci_deployment.js`: contract coverage.
- `README.md`: document pages, multicast behavior, discovery evidence boundary,
  and the fact that media remains ABI v2 until the next plan.

---

### Task 1: Multicast Auto/Custom Contract

**Files:**

- Modify: `src/runtime_config.h`
- Modify: `src/runtime_config.c`
- Modify: `src/gvs_multicast.h`
- Modify: `src/gvs_multicast.c`
- Modify: `src/runtime_service.c`
- Modify: `package/doorfast/files/doorfast.config`
- Modify: `tests/test_runtime_config.c`
- Modify: `tests/test_gvs_multicast.c`

- [ ] **Step 1: Write failing configuration and multicast tests**

Add a configuration fixture that proves `auto` leaves the override empty,
`custom` accepts `239.1.2.3`, and `custom` rejects `192.168.1.1`. Add a
multicast test for an explicit effective group:

```c
struct df_gvs_multicast membership = {.fd = -1};

TEST_ASSERT_INT_EQ(DF_OK, df_gvs_multicast_prepare_group(
    &membership, "239.1.2.3", "10.30.76.0"));
TEST_ASSERT_INT_EQ(0, strcmp("239.1.2.3", membership.group));
TEST_ASSERT_INT_EQ(8300, membership.port);
```

- [ ] **Step 2: Run the focused test binary and confirm failure**

Run:

```bash
make clean
make test
```

Expected: compilation fails because `df_gvs_multicast_prepare_group` and the
new runtime configuration fields do not exist.

- [ ] **Step 3: Add the configuration and effective-group implementation**

Add these runtime fields:

```c
enum df_gvs_multicast_mode {
    DF_GVS_MULTICAST_AUTO = 0,
    DF_GVS_MULTICAST_CUSTOM,
};

enum df_gvs_multicast_mode multicast_mode;
char multicast_address[DF_GVS_IPV4_TEXT_SIZE];
```

Implement:

```c
int df_gvs_multicast_prepare_group(struct df_gvs_multicast *membership,
    const char *group, const char *local_address);
```

Keep `df_gvs_multicast_prepare()` as the auto-derivation wrapper. In runtime
startup, derive the address for `auto`, use the validated configured address
for `custom`, and log both derived and effective values without credentials.

- [ ] **Step 4: Run focused and full C tests**

Run:

```bash
make clean
make test
```

Expected: `build/doorfast-tests` exits 0.

- [ ] **Step 5: Commit**

```bash
git add src/runtime_config.* src/gvs_multicast.* src/runtime_service.c \
  package/doorfast/files/doorfast.config tests/test_runtime_config.c \
  tests/test_gvs_multicast.c
git commit -m "feat: support configurable GVS multicast"
```

### Task 2: Dynamic Station Registry and Legacy Mapping

**Files:**

- Create: `src/station_registry.h`
- Create: `src/station_registry.c`
- Create: `tests/test_station_registry.c`
- Modify: `src/runtime_config.h`
- Modify: `src/runtime_config.c`
- Modify: `tests/test_runtime_config.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

- [ ] **Step 1: Write failing registry tests**

Cover two named sections, duplicate IDs, duplicate logical addresses, duplicate
stream names, `fixed` without IPv4, and an implicit `legacy` station. Use the
public contract:

```c
struct df_station_registry registry = {0};
const struct df_station *station;

TEST_ASSERT_INT_EQ(DF_OK, df_station_registry_load(
    &registry, "tests/fixtures/doorfast-two-stations.conf"));
TEST_ASSERT_INT_EQ(2, registry.count);
station = df_station_registry_find(&registry, "gate_main");
TEST_ASSERT_INT_EQ(0, strcmp("doorfast_gate_main", station->stream_name));
df_station_registry_destroy(&registry);
```

- [ ] **Step 2: Run tests and confirm missing API failure**

Run `make clean && make test`.

Expected: compilation fails on `station_registry.h`.

- [ ] **Step 3: Implement registry ownership and parser integration**

Define focused public types:

```c
enum df_station_route_preference {
    DF_STATION_ROUTE_DISCOVER_FIRST = 0,
    DF_STATION_ROUTE_FIXED,
};

struct df_station {
    char id[33];
    char name[65];
    uint8_t logical_address[6];
    uint32_t configured_ipv4;
    char stream_name[65];
    enum df_station_route_preference route_preference;
    bool enabled;
};

struct df_station_registry {
    struct df_station *items;
    size_t count;
    uint64_t revision;
};
```

Extend the existing UCI text parser so section state distinguishes `gvs main`
from named `station` sections. Allocate exactly the parsed station count after
overflow checks. When no station section exists, synthesize `legacy` from the
three legacy singleton options. Do not write migration from the daemon.

- [ ] **Step 4: Run registry and full test suite**

Run `make clean && make test`.

Expected: all C tests exit 0 and ASAN-style invalid lifetime checks in the new
tests observe no double free during repeated load/destroy.

- [ ] **Step 5: Commit**

```bash
git add src/station_registry.* src/runtime_config.* tests/test_station_registry.c \
  tests/test_runtime_config.c tests/fixtures/doorfast-two-stations.conf \
  tests/test_main.c Makefile
git commit -m "feat: add dynamic door station registry"
```

### Task 3: Verified `07/06` Discovery Engine

**Files:**

- Create: `src/gvs_station_discovery.h`
- Create: `src/gvs_station_discovery.c`
- Create: `tests/test_gvs_station_discovery.c`
- Modify: `src/gvs_udp_sender.h`
- Modify: `src/gvs_udp_sender.c`
- Modify: `tests/test_gvs_udp_sender.c`
- Modify: `tests/test_main.c`
- Modify: `Makefile`

- [ ] **Step 1: Write failing scheduler and admission tests**

The scheduler test must assert exactly three due actions and no fourth action:

```c
struct df_gvs_station_scan scan = {0};
struct df_gvs_station_scan_action action = {0};

TEST_ASSERT_INT_EQ(DF_OK, df_gvs_station_scan_start(
    &scan, identity, 1000U));
TEST_ASSERT_INT_EQ(1, df_gvs_station_scan_next(&scan, 1000U, &action));
TEST_ASSERT_INT_EQ(1, df_gvs_station_scan_next(&scan, 1500U, &action));
TEST_ASSERT_INT_EQ(1, df_gvs_station_scan_next(&scan, 2000U, &action));
TEST_ASSERT_INT_EQ(0, df_gvs_station_scan_next(&scan, 2500U, &action));
```

Admission tests must reject the wrong building/unit, wrong destination, wrong
opcode, multicast source IPv4, and malformed length. Fill 65 unique candidates
and prove the least-recently-seen entry is evicted.

- [ ] **Step 2: Run tests and confirm missing API failure**

Run `make clean && make test`.

Expected: compilation fails on `gvs_station_discovery.h`.

- [ ] **Step 3: Implement scheduler, cache, and UDP broadcast**

Expose:

```c
int df_gvs_station_scan_start(struct df_gvs_station_scan *,
    const uint8_t identity[6], uint64_t now_ms);
int df_gvs_station_scan_next(struct df_gvs_station_scan *, uint64_t now_ms,
    struct df_gvs_station_scan_action *);
int df_gvs_station_discovery_observe(struct df_gvs_station_discovery *,
    const uint8_t *packet, size_t packet_length,
    const uint8_t identity[6], uint64_t now_ms);
int df_gvs_udp_sender_emit_station_scan(struct df_gvs_udp_sender *,
    const struct df_gvs_station_scan_action *);
```

Set `SO_BROADCAST` when opening the active-host sender. Serialize through
`df_gvs_control_serialize()` and the production header provider. The send
function targets `INADDR_BROADCAST`, never a route-cache result.

- [ ] **Step 4: Run complete C tests**

Run `make clean && make test`.

Expected: all tests exit 0, including exact captured frame length and payload
assertions in `test_gvs_udp_sender.c`.

- [ ] **Step 5: Commit**

```bash
git add src/gvs_station_discovery.* src/gvs_udp_sender.* \
  tests/test_gvs_station_discovery.c tests/test_gvs_udp_sender.c \
  tests/test_main.c Makefile
git commit -m "feat: discover GVS door stations"
```

### Task 4: Runtime, ubus, and HTTP Station Contracts

**Files:**

- Modify: `src/runtime_service.c`
- Modify: `src/runtime_ubus.h`
- Modify: `src/runtime_ubus.c`
- Modify: `package/doorfast/files/doorfast-http.sh`
- Modify: `tests/test_runtime_service.c`
- Modify: `tests/test_runtime_ubus.c`
- Modify: `tests/test_doorfast_http.sh`

- [ ] **Step 1: Write failing runtime and JSON contract tests**

Add ubus tests that require `stations`, `station_candidates`, and
`station_scan`. Assert the station payload fields exactly:

```json
{"runtime_id":"0123456789abcdef","revision":1,"stations":[{"id":"gate_main","enabled":true,"route_source":"none","route_fresh":false,"monitorable":false}]}
```

Add shell coverage for `GET /api/v1/stations`; reject POST, request bodies, and
query strings.

- [ ] **Step 2: Run focused failures**

Run:

```bash
make clean && make test
sh tests/test_doorfast_http.sh
```

Expected: C assertions fail because ubus methods are absent; shell test returns
404 for `/api/v1/stations`.

- [ ] **Step 3: Integrate discovery into the event loop**

At startup, create one discovery state from the registry and schedule a scan in
host mode. Tick due send actions without blocking the capture loop. Feed only
validated control packets into the discovery observer, then update the station
route cache. Add ubus handlers:

```c
int df_runtime_ubus_station_list(struct df_runtime_ubus *,
    struct df_station_snapshot *);
int df_runtime_ubus_station_candidates(struct df_runtime_ubus *,
    struct df_station_candidate_snapshot *);
int df_runtime_ubus_station_scan(struct df_runtime_ubus *, uint64_t now_ms);
```

Expose only configured stations at `/api/v1/stations`. Candidate and scan
methods remain ubus-only for LuCI.

- [ ] **Step 4: Verify C and HTTP contracts**

Run:

```bash
make clean && make test
sh tests/test_doorfast_http.sh
```

Expected: both commands exit 0.

- [ ] **Step 5: Commit**

```bash
git add src/runtime_service.c src/runtime_ubus.* \
  package/doorfast/files/doorfast-http.sh tests/test_runtime_service.c \
  tests/test_runtime_ubus.c tests/test_doorfast_http.sh
git commit -m "feat: expose configured door stations"
```

### Task 5: Split LuCI Status, Station, Media, and Deployment Pages

**Files:**

- Create: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/media.js`
- Create: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/stations.js`
- Create: `tests/test_luci_media.js`
- Create: `tests/test_luci_stations.js`
- Modify: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js`
- Modify: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/deployment.js`
- Modify: `package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json`
- Modify: `package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json`
- Modify: `package/luci-app-doorfast/Makefile`
- Modify: `tests/test_luci_status.js`
- Modify: `tests/test_luci_deployment.js`
- Modify: `tests/test_package_manifest.sh`

- [ ] **Step 1: Write failing LuCI tests**

Assert that status source contains no `form.Map`, media options exist only in
`media.js`, multicast custom address depends on custom mode, and station rows
use `form.GridSection`. Simulate RPC calls and require:

```javascript
assert.deepEqual(scanCalls[0], {
    method: 'station_scan',
    args: []
});
assert.equal(validateStationId('row', 'gate_main'), true);
assert.match(validateStationId('row', 'Gate Main'), /小写/);
```

- [ ] **Step 2: Run Node and package checks to confirm failure**

Run:

```bash
node tests/test_luci_status.js
node tests/test_luci_deployment.js
node tests/test_luci_media.js
node tests/test_luci_stations.js
sh tests/test_package_manifest.sh
```

Expected: new files are missing and status still owns media forms.

- [ ] **Step 3: Implement the page split and station workflow**

Move global media fields and RTSP credential writes to `media.js`. Build a
named station grid with enabled, name, logical address, IPv4, route preference,
and stream name. Render candidates separately with an **Adopt** action that
prefills a new station row but requires a normal UCI save before persistence.
Add a scan button that calls `station_scan` and polls candidates. Add
auto/custom multicast fields to deployment. Give menu entries unique orders:
status 10, deployment 20, stations 30, media 40, automation 50, relay 60,
logs 70.

- [ ] **Step 4: Run all LuCI and package tests**

Run:

```bash
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/*.js
node tests/test_luci_status.js
node tests/test_luci_deployment.js
node tests/test_luci_media.js
node tests/test_luci_stations.js
node tests/test_luci_settings.js
node tests/test_luci_relay.js
node tests/test_luci_logs.js
sh tests/test_package_manifest.sh
```

Expected: every command exits 0.

- [ ] **Step 5: Commit**

```bash
git add package/luci-app-doorfast tests/test_luci_*.js \
  tests/test_package_manifest.sh
git commit -m "feat: add dedicated station and media pages"
```

### Task 6: Remove the Separate Media Relay

**Files:**

- Delete: `src/media_relay.h`
- Delete: `src/media_relay.c`
- Delete: `tests/test_media_relay.c`
- Create: `tests/test_media_credential_migration.sh`
- Modify: `src/media_credentials.h`
- Modify: `src/media_credentials.c`
- Modify: `src/media_module.h`
- Modify: `src/media_module.c`
- Modify: `src/runtime_config.c`
- Modify: `src/runtime_ubus.c`
- Modify: `package/doorfast/Makefile`
- Modify: `package/doorfast-media/Makefile`
- Modify: `tests/test_media_credentials.c`
- Modify: `tests/test_media_module.c`
- Modify: `tests/test_runtime_ubus.c`
- Modify: `tests/test_package_manifest.sh`
- Modify: `Makefile`

- [ ] **Step 1: Change tests to the one-relay contract**

Remove media relay events from expected module status. Require media
credentials to contain only `rtsp_password`. Add a migration fixture:

```text
rtsp_password=keep-this
relay_token=remove-this
```

The migration test must assert the output is exactly:

```text
rtsp_password=keep-this
```

- [ ] **Step 2: Run tests and confirm old relay expectations fail**

Run `make clean && make test` and `sh tests/test_package_manifest.sh`.

Expected: assertions fail until relay fields and sources are removed.

- [ ] **Step 3: Remove runtime relay ownership and add atomic migration**

Remove relay URL, token, queue, status, and callbacks from ABI v2 without
renumbering the ABI in this task. Keep call/media notifications on the existing
event stream and HA event relay. Implement package migration with a temporary
file, `chmod 0600`, ownership preservation, `mv`, and cleanup on failure. Never
print either credential value.

- [ ] **Step 4: Run complete main-project verification**

Run:

```bash
make clean && make test
sh tests/test_media_credential_migration.sh
sh tests/test_package_manifest.sh
sh tests/test_doorfast_init.sh
sh tests/test_event_relay_init.sh
```

Expected: every command exits 0 and `rg 'media_relay|relay_token' src package/doorfast-media`
finds no runtime references.

- [ ] **Step 5: Commit**

```bash
git add -A src package/doorfast package/doorfast-media tests Makefile
git commit -m "refactor: consolidate Home Assistant event relay"
```

### Task 7: VM Acceptance and Capability Documentation

**Files:**

- Create: `tests/run_doorfast_vm_stations.py`
- Modify: `tests/test_vm_media_runner.py`
- Modify: `README.md`, the repository's single authoritative status document.

- [ ] **Step 1: Add a failing multi-candidate VM fixture**

Extend the fake VM runner with `station_scan`, three `07/86` candidate replies,
and a configured-station response. Require the acceptance result to include
`scan_sent=3`, three candidates, and only two configured stations in the HTTP
list.

- [ ] **Step 2: Run the VM runner against the fake fixture**

Run:

```bash
python3 -B tests/run_doorfast_vm_stations.py \
  tests/fixtures/fake-vm-preflight-ssh.sh
```

Expected: failure until the fixture and runner expose the new methods.

- [ ] **Step 3: Complete the fixture, runner, and documentation**

Record derived/effective multicast, scan count, redacted candidate data,
configured-station revision, and route freshness. Document active discovery as
software-verified and retain device-side field acceptance as unconfirmed.

- [ ] **Step 4: Run the full stage verification**

Run:

```bash
make clean && make test
sh tests/test_package_manifest.sh
sh tests/test_doorfast_http.sh
node tests/test_luci_status.js
node tests/test_luci_deployment.js
node tests/test_luci_media.js
node tests/test_luci_stations.js
python3 -B tests/run_doorfast_vm_stations.py \
  tests/fixtures/fake-vm-preflight-ssh.sh
git diff --check
```

Expected: every command exits 0.

- [ ] **Step 5: Commit**

```bash
git add tests/run_doorfast_vm_stations.py tests/test_vm_media_runner.py \
  README.md docs
git commit -m "test: validate station discovery lifecycle"
```

After this plan, request review and merge the main-project PR before beginning
the media ABI v3 plan. Do not poll GitHub Actions; wait for the user to report
their result.
