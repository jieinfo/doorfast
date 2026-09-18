# Home Assistant Multi-Station Integration Plan

> **For AI agent workers:** Required sub-skill: use
> `subagent-driven-development` (recommended) or `executing-plans` to implement
> this plan task by task. Track progress with the checkboxes below.

**Goal:** Make one Doorfast Home Assistant config entry enumerate configured
door stations dynamically, expose one reachable sensor and WebRTC camera per
station, and bind every preview lifecycle to that station's runtime and media
generation.

**Architecture:** A station registry coordinator polls
`GET /api/v1/stations`, owns stable station snapshots, and publishes add/remove
signals to entity platforms. Each enabled station owns an independent
`MonitorCoordinator`, while one entry-scoped WebRTC provider parses opaque
station sources and resolves the corresponding coordinator and go2rtc stream.
Polling remains authoritative; relay events accelerate convergence and are
accepted only for the matching runtime, station, generation, and revision.

**Technical stack:** Python 3, Home Assistant config entries and entity
platforms, `aiohttp`, native `CameraWebRTCProvider`, go2rtc WebSocket API,
dependency-free `unittest`, Node.js frontend tests, HACS release workflow.

**Spec:**
`../doorfast/docs/superpowers/specs/2026-09-18-multi-station-media-design.md`

**Prerequisite:** Merge the Doorfast station-list and media ABI v3 HTTP
contracts from
`../doorfast/docs/superpowers/plans/2026-09-18-station-discovery-luci.md` and
`../doorfast/docs/superpowers/plans/2026-09-18-multi-session-media-runtime.md`
before opening the Home Assistant PR.

## Global Constraints

- One Doorfast config entry remains the controller; stations are child devices
  linked with `via_device`.
- Only enabled stations returned by `GET /api/v1/stations` create entities.
- Entity unique IDs use immutable `station_id`, never name, IPv4, logical
  address, list index, or stream name.
- Camera source is
  `doorfast://<entry_id>/station/<station_id>/preview`.
- Every monitor mutation carries `runtime_id + station_id + generation`; start
  carries `runtime_id + station_id` and receives the generation.
- Several HA viewers of one station share one Doorfast source session. Distinct
  stations use distinct coordinators and may run concurrently until Doorfast
  returns a capacity error.
- Answer, hangup, unlock, and elevator operations remain controller/current-call
  operations and do not infer a target from the viewed camera.
- Polling is authoritative after relay loss, reconnect, or conflicting events.
- go2rtc credentials, RTSP passwords, SDP, ICE candidates, and private endpoint
  query strings must not enter HA state or logs.
- Do not claim device-side concurrent monitoring until field PCAP acceptance
  succeeds with two real stations.
- PR titles and commits are in English.

---

## File Structure

**Create:**

- `custom_components/doorfast/stations.py`: station parsing, revision-aware
  registry coordinator, monitor ownership, and entity add/remove signals.
- `custom_components/doorfast/station_entity.py`: shared child-device and
  station availability behavior.
- `tests/test_stations.py`: list validation, revisions, add/disable/remove,
  stable identity, and coordinator cleanup.
- `tests/test_station_reachability.py`: reachability entity and route changes.

**Modify:**

- `custom_components/doorfast/client_types.py`: immutable station and station
  snapshot value types.
- `custom_components/doorfast/client.py`: station list and station-keyed monitor
  HTTP operations.
- `custom_components/doorfast/monitor.py`: bind one coordinator to runtime and
  station IDs and reject mismatched generations.
- `custom_components/doorfast/camera.py`: dynamically add one station camera and
  use the station-scoped opaque source.
- `custom_components/doorfast/webrtc.py`: parse station sources, select the
  matching coordinator and stream, and build a configurable WS(S) URL.
- `custom_components/doorfast/binary_sensor.py`: retain incoming-call state and
  dynamically add station reachability entities.
- `custom_components/doorfast/events.py`: validate and gate station-scoped media
  events independently.
- `custom_components/doorfast/__init__.py`: create/close the registry, reconcile
  polling and relay state, and register one multi-station WebRTC provider.
- `custom_components/doorfast/setup_lifecycle.py`: roll back and unload every
  station coordinator and provider session exactly once.
- `custom_components/doorfast/config_flow.py`: configure and update the go2rtc
  API base URL.
- `custom_components/doorfast/config_helpers.py`: normalize and validate the
  go2rtc API URL without retaining path/query credentials.
- `custom_components/doorfast/const.py`: station signals, registry key, and
  `CONF_GO2RTC_API_URL`.
- `custom_components/doorfast/services.yaml`: require `station_id` for explicit
  monitor start/stop services.
- `custom_components/doorfast/translations/en.json` and
  `custom_components/doorfast/translations/zh-Hans.json`: option and entity
  labels.
- `custom_components/doorfast/manifest.json`: release version after acceptance.
- `doorfast_ha_e2e/fixture.py`, `doorfast_ha_e2e/runner.py`: multi-station
  fixture and acceptance checks.
- `tests/test_client.py`, `tests/test_monitor.py`, `tests/test_camera.py`,
  `tests/test_webrtc.py`, `tests/test_events.py`,
  `tests/test_setup_lifecycle.py`, `tests/test_config_helpers.py`,
  `tests/test_e2e_runner.py`, `tests/test_release_metadata.py`: contract and
  lifecycle coverage.
- `docs/go2rtc-webrtc-acceptance.md`: per-station stream mapping and evidence
  boundary.
- `README.md`: configuration, dynamic station entities, and concurrency limits.

---

### Task 1: Station and Keyed Monitor Client Contracts

**Files:**

- Modify: `custom_components/doorfast/client_types.py`
- Modify: `custom_components/doorfast/client.py`
- Modify: `tests/test_client.py`

- [ ] **Step 1: Write failing station parsing and request tests**

Add tests for a valid station list, duplicate IDs, wrong runtime IDs, disabled
stations, malformed fields, and keyed monitor payloads. Require this public
shape:

```python
station = DoorfastStation.from_payload({
    "id": "gate_main",
    "name": "Main Gate",
    "logical_address": "32:02:01:00:02:00",
    "enabled": True,
    "stream_name": "doorfast_gate_main",
    "route_source": "discovered",
    "route_fresh": True,
    "monitorable": True,
    "last_seen_ms": 123456,
})
self.assertEqual("gate_main", station.station_id)
self.assertTrue(station.reachable)
```

Also assert exact request bodies:

```python
await client.start_monitor("runtime-a", "gate_main")
await client.stop_monitor("runtime-a", "gate_main", 21)
await client.set_monitor_viewer("runtime-a", "gate_main", 21, True)
```

- [ ] **Step 2: Run the focused tests and confirm failure**

Run:

```bash
python3 -B -m unittest tests.test_client
```

Expected: imports or assertions fail because station types and station-keyed
methods do not exist.

- [ ] **Step 3: Implement strict immutable transport types**

Add:

```python
@dataclass(frozen=True, slots=True)
class DoorfastStation:
    station_id: str
    name: str
    logical_address: str
    enabled: bool
    stream_name: str
    route_source: str
    route_fresh: bool
    monitorable: bool
    last_seen_ms: int | None

    @property
    def reachable(self) -> bool:
        return self.enabled and self.route_fresh and self.monitorable

    @classmethod
    def from_payload(cls, payload: dict[str, Any]) -> "DoorfastStation":
        return cls(
            station_id=require_station_id(payload.get("id")),
            name=require_text(payload.get("name"), "name", 128),
            logical_address=require_logical_address(payload.get("logical_address")),
            enabled=require_bool(payload.get("enabled"), "enabled"),
            stream_name=require_stream_name(payload.get("stream_name")),
            route_source=require_route_source(payload.get("route_source")),
            route_fresh=require_bool(payload.get("route_fresh"), "route_fresh"),
            monitorable=require_bool(payload.get("monitorable"), "monitorable"),
            last_seen_ms=optional_non_negative_int(payload.get("last_seen_ms")),
        )

@dataclass(frozen=True, slots=True)
class DoorfastStationSnapshot:
    runtime_id: str
    revision: int
    stations: tuple[DoorfastStation, ...]
```

`DoorfastClient.stations()` calls `GET /api/v1/stations` and returns a validated
snapshot. Make `start_monitor`, `stop_monitor`, and `set_monitor_viewer` include
the exact runtime/station/generation tuple required by the main-project API.
Define the referenced `require_station_id`, `require_text`,
`require_logical_address`, `require_stream_name`, `require_route_source`,
`require_bool`, and `optional_non_negative_int` helpers in `client_types.py`;
they accept only the schema in section 9.1 of the spec, keep text ASCII-bounded,
and reject booleans where integers are expected.

- [ ] **Step 4: Run focused and full tests**

Run:

```bash
python3 -B -m unittest tests.test_client
python3 -B -m unittest discover -s tests
```

Expected: all client and existing regression tests pass.

- [ ] **Step 5: Commit**

```bash
git add custom_components/doorfast/client.py \
  custom_components/doorfast/client_types.py tests/test_client.py
git commit -m "feat: add station-aware Doorfast client"
```

### Task 2: Station Registry and Entity Lifecycle

**Files:**

- Create: `custom_components/doorfast/stations.py`
- Create: `custom_components/doorfast/station_entity.py`
- Create: `tests/test_stations.py`
- Modify: `custom_components/doorfast/const.py`
- Modify: `custom_components/doorfast/__init__.py`
- Modify: `custom_components/doorfast/setup_lifecycle.py`
- Modify: `tests/test_setup_lifecycle.py`

- [ ] **Step 1: Write failing registry lifecycle tests**

Test initial enumeration, name/IP changes without identity changes, new station
signals, disable/removal signals, unchanged revisions, runtime restart, and
coordinator cleanup. Use the stable registry contract:

```python
registry = StationRegistryCoordinator(client, entry_id="entry-1")
await registry.async_refresh()
self.assertEqual(("gate_main", "gate_side"), registry.station_ids)
self.assertIs(
    registry.monitor("gate_main"),
    registry.monitor("gate_main"),
)
```

- [ ] **Step 2: Run focused tests and confirm the module is missing**

Run `python3 -B -m unittest tests.test_stations`.

Expected: import fails for `custom_components.doorfast.stations`.

- [ ] **Step 3: Implement revision-aware registry ownership**

`StationRegistryCoordinator` stores the current `DoorfastStationSnapshot`, one
`MonitorCoordinator` per enabled station, and listeners for `added`, `updated`,
and `removed` station IDs. Ignore an unchanged `(runtime_id, revision)` pair.
On runtime change, close every active coordinator before rebuilding from the
new snapshot. On disable/removal, close that station's coordinator before
publishing removal. Never reuse a removed station object for a different ID.

`DoorfastStationEntity` exposes the station child device:

```python
return {
    "identifiers": {(DOMAIN, entry_id, station.station_id)},
    "name": station.name,
    "manufacturer": MANUFACTURER,
    "via_device": (DOMAIN, entry_id),
}
```

Use one `STATIONS_KEY` entry per config entry and ensure setup rollback removes
the registry, its monitors, and listeners without disturbing another config
entry.

- [ ] **Step 4: Run lifecycle and complete tests**

Run:

```bash
python3 -B -m unittest tests.test_stations tests.test_setup_lifecycle
python3 -B -m unittest discover -s tests
```

Expected: station and existing entry lifecycle tests pass.

- [ ] **Step 5: Commit**

```bash
git add custom_components/doorfast/stations.py \
  custom_components/doorfast/station_entity.py \
  custom_components/doorfast/const.py custom_components/doorfast/__init__.py \
  custom_components/doorfast/setup_lifecycle.py tests/test_stations.py \
  tests/test_setup_lifecycle.py
git commit -m "feat: manage dynamic Doorfast stations"
```

### Task 3: Per-Station Cameras and Monitor Coordinators

**Files:**

- Modify: `custom_components/doorfast/monitor.py`
- Modify: `custom_components/doorfast/camera.py`
- Modify: `custom_components/doorfast/__init__.py`
- Modify: `custom_components/doorfast/services.yaml`
- Modify: `tests/test_monitor.py`
- Modify: `tests/test_camera.py`
- Modify: `tests/test_stations.py`

- [ ] **Step 1: Write failing station isolation tests**

Create two station coordinators, start both, and prove each emitted body uses
its own `runtime_id` and `station_id`. Test generation mismatch, capacity-busy
failure, one station preemption without changing the other, same-station viewer
reference sharing, and exact camera identity:

```python
self.assertEqual(
    "doorfast://entry-1/station/gate_main/preview",
    await camera.stream_source(),
)
self.assertEqual(
    "doorfast_entry-1_station_gate_main_camera",
    camera.unique_id,
)
```

- [ ] **Step 2: Run focused tests and confirm singleton assumptions fail**

Run:

```bash
python3 -B -m unittest tests.test_monitor tests.test_camera tests.test_stations
```

Expected: requests omit station keys and camera setup creates only one entity.

- [ ] **Step 3: Bind coordinators and cameras to stable station IDs**

Construct `MonitorCoordinator(client, runtime_id, station_id, ...)`. Keep the
existing lock, ready wait, viewer reference count, grace stop, and idempotent
unload behavior. Pass the full mutation tuple through every client call and
reject a status or event whose runtime, station, or active generation does not
match the coordinator.

Camera setup subscribes to registry add/remove events. Add one
`DoorfastStationCamera` for each enabled station; removed entities become
unavailable, call `async_remove(force_remove=True)`, and remove their entity
registry record before the station object is discarded. The main-project
contract does not expose a station-keyed JPEG endpoint, so
`async_camera_image()` returns `None`; it must never reuse the legacy singleton
snapshot and risk showing another station's frame.

Add required `station_id` fields to explicit `start_monitor` and `stop_monitor`
services. Controller call services remain unchanged.

- [ ] **Step 4: Run focused and full tests**

Run:

```bash
python3 -B -m unittest tests.test_monitor tests.test_camera tests.test_stations
python3 -B -m unittest discover -s tests
```

Expected: independent coordinators and dynamic cameras pass with all legacy
control tests intact.

- [ ] **Step 5: Commit**

```bash
git add custom_components/doorfast/monitor.py \
  custom_components/doorfast/camera.py custom_components/doorfast/__init__.py \
  custom_components/doorfast/services.yaml tests/test_monitor.py \
  tests/test_camera.py tests/test_stations.py
git commit -m "feat: add per-station monitor cameras"
```

### Task 4: Configurable Station-Aware WebRTC

**Files:**

- Modify: `custom_components/doorfast/const.py`
- Modify: `custom_components/doorfast/config_helpers.py`
- Modify: `custom_components/doorfast/config_flow.py`
- Modify: `custom_components/doorfast/webrtc.py`
- Modify: `custom_components/doorfast/__init__.py`
- Modify: `tests/test_config_helpers.py`
- Modify: `tests/test_webrtc.py`
- Modify: `tests/test_go2rtc_contract.py`

- [ ] **Step 1: Write failing URL and routing tests**

Cover `http://ha.local:1984`, `https://go2rtc.example/base`, invalid schemes,
embedded credentials, fragments, station source parsing, stream-name URL
encoding, two simultaneous station offers, stale-generation cleanup, and one
station failure that leaves the other open.

Expected WebSocket selection:

```python
self.assertEqual(
    "wss://go2rtc.example/base/api/ws?src=doorfast_gate_main",
    provider.websocket_url("doorfast_gate_main"),
)
```

- [ ] **Step 2: Run focused tests and confirm fixed URL behavior**

Run:

```bash
python3 -B -m unittest tests.test_config_helpers tests.test_webrtc \
  tests.test_go2rtc_contract
```

Expected: tests fail because the provider still uses the fixed
`doorfast_preview` URL and accepts only the singleton source.

- [ ] **Step 3: Implement safe go2rtc configuration and station resolution**

Add `CONF_GO2RTC_API_URL = "go2rtc_api_url"`, defaulting to
`http://127.0.0.1:1984`. Normalize only HTTP(S) base URLs, reject username,
password, query, and fragment, preserve an optional path prefix, and convert
HTTP/HTTPS to WS/WSS only when constructing `/api/ws?src=<quoted stream_name>`.

The provider parses only:

```text
doorfast://<own-entry-id>/station/<known-station-id>/preview
```

It resolves the station from the current registry, acquires that station's
coordinator, and records both station ID and generation in each WebRTC session.
Reconciliation closes only sessions whose station disappeared or whose exact
generation is stale.

Expose the go2rtc URL through a config-entry options flow so existing entries
can change it without deleting and recreating the integration.

- [ ] **Step 4: Run focused and full tests**

Run:

```bash
python3 -B -m unittest tests.test_config_helpers tests.test_webrtc \
  tests.test_go2rtc_contract
python3 -B -m unittest discover -s tests
```

Expected: HTTP and HTTPS bases produce correct per-station WS(S) URLs and
concurrent station sessions remain isolated.

- [ ] **Step 5: Commit**

```bash
git add custom_components/doorfast/const.py \
  custom_components/doorfast/config_helpers.py \
  custom_components/doorfast/config_flow.py custom_components/doorfast/webrtc.py \
  custom_components/doorfast/__init__.py tests/test_config_helpers.py \
  tests/test_webrtc.py tests/test_go2rtc_contract.py
git commit -m "feat: route WebRTC by Doorfast station"
```

### Task 5: Reachability Entities and Event/Poll Convergence

**Files:**

- Create: `tests/test_station_reachability.py`
- Modify: `custom_components/doorfast/binary_sensor.py`
- Modify: `custom_components/doorfast/events.py`
- Modify: `custom_components/doorfast/__init__.py`
- Modify: `custom_components/doorfast/setup_lifecycle.py`
- Modify: `tests/test_events.py`
- Modify: `tests/test_setup_lifecycle.py`

- [ ] **Step 1: Write failing reachability and event-order tests**

Test one reachability entity per enabled station, route expiry and recovery,
stable unique IDs after name/IP changes, station removal, relay events for two
stations with equal generations, duplicate station events, wrong runtime,
wrong station, stale revision, relay disconnect, and later poll convergence.

Require:

```python
self.assertEqual(
    "doorfast_entry-1_station_gate_main_reachable",
    entity.unique_id,
)
self.assertFalse(entity.is_on)  # route_fresh=False
```

- [ ] **Step 2: Run focused tests and confirm station event support is absent**

Run:

```bash
python3 -B -m unittest tests.test_station_reachability tests.test_events \
  tests.test_setup_lifecycle
```

Expected: the singleton event high-water mark conflicts across stations and no
reachability entity exists.

- [ ] **Step 3: Implement station-scoped reachability and event gates**

Keep the controller incoming-call sensor. Dynamically add
`DoorfastStationReachability` entities whose state derives from the current
station snapshot and whose child device/unique ID use the immutable station ID.

Extend media-event validation to require `station_id` for configured-station
monitor and encoder events. Key duplicate/high-water state by
`(runtime_id, station_id)` and validate generation plus status revision per
station. After an accepted relay event, refresh authoritative station and
monitor status, then dispatch only to that station. Polling must correct any
missed, stale, or conflicting relay update.

- [ ] **Step 4: Run focused and full tests**

Run:

```bash
python3 -B -m unittest tests.test_station_reachability tests.test_events \
  tests.test_setup_lifecycle
python3 -B -m unittest discover -s tests
```

Expected: equal generations from separate stations do not conflict, stale
events cannot mutate another coordinator, and polling restores current state.

- [ ] **Step 5: Commit**

```bash
git add custom_components/doorfast/binary_sensor.py \
  custom_components/doorfast/events.py custom_components/doorfast/__init__.py \
  custom_components/doorfast/setup_lifecycle.py \
  tests/test_station_reachability.py tests/test_events.py \
  tests/test_setup_lifecycle.py
git commit -m "feat: expose station reachability and events"
```

### Task 6: Translation, Acceptance, and HACS Release Preparation

**Files:**

- Modify: `custom_components/doorfast/translations/en.json`
- Modify: `custom_components/doorfast/translations/zh-Hans.json`
- Modify: `custom_components/doorfast/manifest.json`
- Modify: `doorfast_ha_e2e/fixture.py`
- Modify: `doorfast_ha_e2e/runner.py`
- Modify: `tests/test_e2e_runner.py`
- Modify: `tests/test_release_metadata.py`
- Modify: `docs/go2rtc-webrtc-acceptance.md`
- Modify: `README.md`

- [ ] **Step 1: Write failing metadata and multi-station fixture tests**

Require the fixture to return at least two configured stations, independent
monitor generations, capacity-busy errors, preemption, and per-station stream
names. Assert translations contain go2rtc option and station entity keys, and
release metadata agrees on version `0.3.0`.

- [ ] **Step 2: Run acceptance-focused tests and confirm missing coverage**

Run:

```bash
python3 -B -m unittest tests.test_e2e_runner tests.test_release_metadata
```

Expected: assertions fail because the fixture and documentation describe only
the singleton `doorfast_preview` stream.

- [ ] **Step 3: Update fixtures, documentation, and version metadata**

Teach the test-only fixture the production station-list and monitor contracts.
The runner must verify initial enumeration, dynamic add/disable/remove,
distinct station cameras, same-station viewer sharing, two distinct concurrent
sessions, a capacity error, incoming-call preemption, relay/poll convergence,
and cleanup. Redact tokens, endpoint query strings, SDP, ICE, and credentials.

Document a go2rtc `streams` entry for every configured station stream name and
state that `max_encoders` limits station source sessions, not browser viewers.
Keep the field boundary explicit: simulated concurrency does not prove real
door-station acceptance.

Bump `manifest.json` to `0.3.0`, but do not create a tag or GitHub release in
this task.

- [ ] **Step 4: Run all Actions-equivalent checks**

Run:

```bash
node --test tests/js/*.mjs
python3 -m compileall -q custom_components/doorfast
python3 -B -m unittest discover -s tests
python3 - <<'PY'
import json
from pathlib import Path
for path in Path('.').rglob('*.json'):
    json.load(path.open())
PY
git diff --check
```

Expected: every command exits 0.

- [ ] **Step 5: Commit and prepare delivery**

```bash
git add custom_components/doorfast/translations \
  custom_components/doorfast/manifest.json doorfast_ha_e2e \
  tests/test_e2e_runner.py tests/test_release_metadata.py \
  docs/go2rtc-webrtc-acceptance.md README.md
git commit -m "docs: prepare multi-station HA release"
```

Push an English-titled PR only after rebasing on current `origin/main`. Do not
poll GitHub Actions; wait for the user to report completion. Create the HACS
tag and GitHub release only after the HA PR is merged and its Actions pass.
