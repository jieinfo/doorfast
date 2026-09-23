# Doorfast Multi-Station Discovery and Media Design

Date: 2026-09-18

## 1. Purpose

Doorfast currently exposes one configured door station, one monitor state, and
one encoder. Media settings are embedded in the LuCI status page, the GVS
multicast group can only be derived from the indoor identity, and Home
Assistant creates one camera backed by a fixed go2rtc stream name.

This design introduces:

- a read-only LuCI status page and dedicated deployment, station, and media
  pages;
- automatic or explicitly overridden residential multicast configuration;
- active `07/06` station discovery with manually approved candidates;
- an arbitrary number of configured door stations;
- a user-selected limit for concurrent source encoders;
- one independent media session and go2rtc stream per active station;
- Home Assistant entities derived from Doorfast's configured station list.

The design preserves the distinction between protocol submission, protocol
reply, and physical action. Station discovery proves only that a matching
`07/86` reply was observed. It is not a registration lease or proof of a
physical installation.

## 2. Current Constraints

The design is based on `doorfast` commit
`5fb95698e3a54b5d8297e403bbfddf1f40c931ef` and `doorfastforha` commit
`858109525f1339aa50e30dfb402dd2170b7fb9dd`.

The current implementation has these relevant constraints:

- `media_station_address`, `media_station_ipv4`, and `media_stream_name` are
  singleton UCI options.
- media ABI v2 owns one station, monitor, JPEG pipeline, and encoder.
- a fresh incoming `07/86` can populate a four-entry preview route cache, but
  Doorfast does not emit `07/06` discovery requests.
- monitor start accepts an empty request and cannot select a station.
- Home Assistant creates one camera and uses the fixed source
  `doorfast://<entry_id>/preview`.
- the WebRTC provider connects to the fixed go2rtc WebSocket URL
  `ws://127.0.0.1:1984/api/ws?src=doorfast_preview`.
- multiple Home Assistant viewers already share one source encoder through
  go2rtc; viewer count is not the same as source encoder count.

## 3. Evidence Boundary

Vendor static material and the available PCAP establish the following
`0x32` door-station discovery profile:

- family/opcode: `07/06` request and `07/86` reply;
- UDP destination: `255.255.255.255:8300`;
- logical destination: `32:<building>:<unit>:FF:FF:FF`;
- request payload: `02 00 00 01`;
- burst: three requests at approximately 0, 500, and 1000 milliseconds;
- reply: a station logical address and the source IPv4 from the received UDP
  datagram.

Doorfast will initially implement only the `0x32` door-station profile.
`0x13` perimeter-gate discovery remains out of scope until its monitoring and
media behavior is validated.

The supplied evidence contains no concurrent active-monitor sample from two
door stations. The software may support parallel sessions, but device-side
parallel `03/04` acceptance must be validated with a bidirectional field PCAP
before documentation describes it as field-confirmed.

## 4. LuCI Information Architecture

The Doorfast menu will contain these pages:

1. **Status**: read-only runtime, call, station, discovery, encoder, and error
   state.
2. **Deployment**: passive/host mode, interfaces, GVS identity, indoor address,
   and multicast settings.
3. **Door Stations**: configured stations, observed candidates, scan action,
   route source, freshness, and last-seen state.
4. **Media Preview**: global go2rtc, encoder, capacity, resource, and credential
   settings.
5. **Incoming Call Automation**.
6. **HA Event Relay**.
7. **Logs**.

The status page will contain no editable form. The media event relay URL and
media relay token will be removed from LuCI and from the media runtime. go2rtc
handles media only. Doorfast-to-HA call and media state events use the existing
HA event relay, with polling as the authoritative fallback.

## 5. Multicast Configuration

The `gvs` UCI section gains:

```text
option gvs_multicast_mode 'auto'
option gvs_multicast_address ''
```

`gvs_multicast_mode` accepts `auto` or `custom`.

- `auto` derives the residential group from the configured GVS identity with
  `df_gvs_identity_multicast_ip()`.
- `custom` requires `gvs_multicast_address` to be a valid IPv4 multicast
  address in `224.0.0.0/4`.

LuCI always displays the derived address and, in custom mode, the effective
override. UDP port `8300` remains fixed. The runtime status reports mode,
derived address, effective address, and membership state. These settings are
shown only for host mode.

An invalid custom address prevents host mode from starting and produces a
specific preflight error. Doorfast never silently falls back from an invalid
custom address to the derived address.

## 6. Station Configuration Model

Each configured door station is a named UCI section:

```text
config station 'gate_main'
        option enabled '1'
        option name 'Main Gate'
        option logical_address '32:02:01:00:02:00'
        option ipv4 ''
        option route_preference 'discover_first'
        option stream_name 'doorfast_gate_main'
```

The UCI section name is the immutable `station_id`. It must be an ASCII
identifier composed of lowercase letters, digits, and underscores, start with
a letter, and be no longer than 32 bytes. The display name can change without
changing Home Assistant unique IDs.

Station sections are dynamically allocated. There is no compile-time limit on
the number of configured stations. Runtime allocation is bounded by the
number of successfully parsed station sections, and size arithmetic must reject
overflow before allocating memory.

Each station requires:

- an enabled flag;
- a display name;
- a valid six-byte `0x32` logical address;
- a unique stream name.

The IPv4 address is optional. `route_preference` accepts:

- `discover_first`: use a fresh discovered route, then the configured IPv4;
- `fixed`: use the configured IPv4 while still recording discovery results.

The runtime rejects duplicate station IDs, logical addresses, or stream names.
LuCI generates `doorfast_<station_id>` as the default stream name and permits
an explicit override. A station using `fixed` route preference must provide a
valid unicast IPv4 address.
An enabled station without either a fresh discovered route or configured IPv4
remains visible but is not monitorable.

## 7. Discovery Lifecycle

### 7.1 Scan triggers

Active discovery is available only in host mode. A scan can be triggered by:

- the LuCI **Scan Door Stations** command;
- runtime startup when discovery is enabled;
- an on-demand monitor request whose station has no usable route.

There is no unconditional periodic broadcast. The available evidence does not
establish a fixed scan interval.

### 7.2 Wire behavior

The UDP sender enables `SO_BROADCAST` and schedules exactly three `07/06`
frames at 0, 500, and 1000 milliseconds. The serializer uses the production
vendor header provider, the current indoor identity as source, the derived
building/unit broadcast logical destination, and payload `02 00 00 01`.

Doorfast accepts a discovery reply only when all of these checks pass:

- complete UDP/8300 control frame;
- family/opcode `07/86`;
- destination matches the current indoor identity;
- source is a valid `0x32` station address for the configured building/unit;
- source IPv4 is unicast;
- frame length and declared payload shape match the accepted installation
  profile.

### 7.3 Candidate handling

Valid replies update an in-memory least-recently-used candidate cache with 64
entries. This bound applies only to unapproved broadcast observations; it does
not limit configured station sections. The cache stores logical address, IPv4,
first-seen time, last-seen time, reply count, and whether the address already
maps to a configured station.

Candidates are never persisted automatically. LuCI requires the user to select
**Adopt**, choose a stable station ID and name, and save the resulting station
section. This prevents unrelated devices on the shared broadcast network from
appearing automatically in Home Assistant.

Configured station routes retain the current freshness rule: a discovered
route is usable for 60 seconds. The configured fixed IPv4 has no freshness
expiry. Expiry changes route availability; it does not delete the station.

## 8. Multi-Session Media ABI

The media ABI advances from v2 to v3. ABI v3 replaces the singleton station,
monitor, reassembler, and encoder with a global manager plus dynamically
allocated station sessions.

### 8.1 Global configuration

The global configuration contains:

- the station registry;
- go2rtc host and RTSP port;
- shared RTSP credentials;
- encoder type, resolution, frame rate, bitrate, and profile;
- first-frame and preview timeouts;
- `max_encoders`;
- minimum free memory;
- incoming-call priority policy.

LuCI labels `max_encoders` as **Maximum simultaneous source streams**. It
defaults to 1 and must be between 1 and the number of enabled stations. The
runtime does not clamp it to a hidden hardware-specific constant. The
effective capacity is:

```text
min(configured max_encoders, enabled station count)
```

When media is disabled, a deployment may have no enabled stations and retains
the default value for later use. Enabling media requires at least one enabled
station and revalidates `max_encoders` against the enabled station count.

Starting a source still requires the minimum-free-memory check and successful
encoder process creation. A configured capacity is an upper bound, not a
guarantee that the hardware can sustain that many encoders.

### 8.2 Session ownership

Each active station session owns:

- `station_id` and a copy of the station route snapshot;
- a runtime-unique 64-bit generation;
- purpose: `preview` or `call`;
- monitor protocol state and deadlines;
- independent JPEG reassembly and frame queue state;
- one supervised FFmpeg encoder process;
- one go2rtc RTSP stream name;
- viewer state, timestamps, counters, and last failure.

UDP/8303 frames are demultiplexed by exact source station logical address and
local destination identity before entering a session reassembler. A malformed
or unknown source cannot feed another station's encoder.

Failure or timeout in one session stops only that session. It does not stop
other encoders or erase other station routes.

### 8.3 Admission rules

- Starting a station that already has an active source session reuses that
  session and go2rtc producer.
- Starting another station below capacity creates a new session and encoder.
- A request at capacity returns `capacity_busy` and identifies configured,
  effective, and active capacity.
- A failed memory guard returns `resource_exhausted`.
- A missing route after an on-demand discovery burst returns
  `route_unavailable`.
- Encoder startup failure returns `encoder_failed` and releases the slot.

Multiple Home Assistant viewers of one station do not consume additional
source slots. go2rtc fans out the single station stream.

### 8.4 Incoming-call priority

LuCI exposes an **Incoming call media priority** setting:

- `preempt_oldest_preview` is the default;
- `preserve_previews` is the alternative.

The total configured encoder limit covers both proactive preview and incoming
call video.

When a call arrives from a station that already has a preview session, the
runtime cancels that station's proactive monitor transaction, keeps the same
capacity slot and stream path, clears queued preview frames, and binds the
session to the call generation. The encoder process may be restarted as an
internal recovery step, but status never counts two source encoders for this
transition. Existing viewers are notified of the generation change and
reconnect through the same station stream.

When a call arrives from another station and the pool is full:

- `preempt_oldest_preview` stops the oldest proactive preview, releases its
  encoder, and assigns the slot to the incoming call;
- `preserve_previews` keeps the previews. Call notification and control still
  work, but call video reports `capacity_busy` until a slot is available.

A call session is never selected as the victim of preview admission.

### 8.5 Generation and runtime identity

Every media mutation requires the tuple:

```text
runtime_id + station_id + generation
```

The tuple prevents a restarted process or another station's numerically equal
generation from accepting a stale stop or viewer command. Start requires
`runtime_id` and `station_id`; the returned generation completes the tuple.

## 9. Runtime and HTTP Contracts

### 9.1 Station list

Doorfast exposes `stations` over ubus and `GET /api/v1/stations`:

```json
{
  "runtime_id": "0123456789abcdef",
  "revision": 3,
  "stations": [
    {
      "id": "gate_main",
      "name": "Main Gate",
      "logical_address": "32:02:01:00:02:00",
      "enabled": true,
      "stream_name": "doorfast_gate_main",
      "route_source": "discovered",
      "route_fresh": true,
      "monitorable": true,
      "last_seen_ms": 123456
    }
  ]
}
```

Only configured stations appear in this endpoint. Discovery candidates use a
separate LuCI/ubus-only method and are not exposed to Home Assistant.

`revision` changes whenever the enabled station set or any HA-relevant station
field changes.

### 9.2 Monitor operations

The HTTP operations become:

```text
POST /api/v1/monitor/start
  {runtime_id, station_id}

POST /api/v1/monitor/stop
  {runtime_id, station_id, generation}

POST /api/v1/monitor/viewer
  {runtime_id, station_id, generation, active}

GET /api/v1/monitor/status
```

Monitor status returns global capacity and a session array:

```json
{
  "configured_capacity": 3,
  "effective_capacity": 3,
  "active_encoders": 2,
  "sessions": [
    {
      "station_id": "gate_main",
      "generation": 21,
      "purpose": "preview",
      "state": "viewing",
      "ready": true,
      "viewer_active": true,
      "stream_name": "doorfast_gate_main"
    }
  ]
}
```

Events for incoming calls, call establishment, hangup, monitor state, encoder
state, and preemption include `station_id` when the source maps to a configured
station. Unknown sources retain the raw logical address and do not invent a
station ID.

## 10. Home Assistant Integration

Home Assistant treats one Doorfast config entry as the controller and derives
station devices from `GET /api/v1/stations`.

For each enabled station it creates:

- one camera entity;
- one station reachability binary sensor;
- a child device linked to the Doorfast controller with `via_device`.

Entity unique IDs use the immutable station ID:

```text
<entry_id>_station_<station_id>_camera
<entry_id>_station_<station_id>_reachable
```

HA polls the station revision during the existing Doorfast status cycle. A new
station is added without recreating the config entry. A removed or disabled
station becomes unavailable and is removed from the runtime entity set without
reusing its unique ID for another station.

Each station has an independent monitor coordinator. Camera sources use:

```text
doorfast://<entry_id>/station/<station_id>/preview
```

The WebRTC provider resolves the station's `stream_name` instead of using
`doorfast_preview`. The go2rtc API base URL becomes a Home Assistant integration
option so non-container installations are not forced to use
`127.0.0.1:1984`.

Opening cameras for distinct stations in parallel starts distinct source
sessions until Doorfast reaches its configured capacity. Opening the same
camera in several browsers increments HA viewer references but continues to
use one Doorfast source session.

Answer, hangup, unlock, and elevator controls remain controller/call-session
operations. They continue to use the current call generation and do not infer
a target merely from the camera being viewed. A per-station unlock control is
outside this design until active-monitor unlock semantics are field-validated.

## 11. Compatibility and Migration

When no `station` sections exist, the runtime maps the legacy singleton
`media_station_address`, `media_station_ipv4`, and `media_stream_name` options
to an implicit station with ID `legacy`. LuCI displays this station and converts
it to an explicit station section on the first station-page save.

Legacy media options remain readable for one compatibility release but are no
longer written. ABI v2 modules are rejected with an explicit version error;
the core and media APKs must be upgraded together.

Legacy `media_relay_url` is ignored. The media credential loader preserves the
RTSP password and stops exposing or using the legacy relay token. Package
migration removes only the relay-token entry after successfully rewriting the
credential file; a failed rewrite leaves the original file intact and records
a migration warning.

The default capacity of 1 preserves current resource use until the user
explicitly increases it.

## 12. Error and Status Semantics

All API errors use stable machine-readable codes and a human-readable message.
Relevant codes are:

- `station_not_found`;
- `station_disabled`;
- `route_unavailable`;
- `capacity_busy`;
- `resource_exhausted`;
- `encoder_failed`;
- `video_stalled` when a publishing or viewed source stops producing complete
  JPEG frames for thirty seconds. The longer window is required because the
  door stations publish JPEG frames in bursts with observed gaps above twenty
  seconds;
- `generation_mismatch`;
- `runtime_mismatch`;
- `session_preempted`.

`submitted=true` means the local protocol action was queued or sent. It does
not mean a door station accepted it. `route_fresh=true` means a valid reply was
recently observed. It does not mean the station is registered or that video
will start.

## 13. Verification

### 13.1 Main project tests

Unit and integration tests cover:

- multicast auto/custom parsing, validation, and effective status;
- dynamic station parsing, duplicate rejection, migration, and overflow;
- exact `07/06` destination, payload, three-send timing, and broadcast socket;
- strict `07/86` filtering, candidate deduplication, freshness, and expiry;
- independent frame demultiplexing for at least three stations;
- capacity 1, capacity N, same-station reuse, and full-pool rejection;
- minimum-memory rejection and encoder-start rollback;
- incoming-call reuse, oldest-preview preemption, and preserve policy;
- failure isolation between encoder sessions;
- runtime/station/generation mismatch rejection;
- ubus and HTTP JSON contracts;
- LuCI page placement, validation, scan/adopt flow, and credential redaction.

### 13.2 Home Assistant tests

The HA suite covers:

- initial station enumeration;
- dynamic add, disable, and removal behavior;
- stable entity IDs across display-name and IP changes;
- independent station coordinators and concurrent cameras;
- same-station multi-viewer sharing;
- capacity, preemption, and generation-change handling;
- configurable go2rtc API URL and per-station stream selection;
- event and polling convergence after relay disconnect.

### 13.3 Software and VM acceptance

The ImmortalWrt VM preflight verifies the installed Doorfast service and ABI v3
media module are available. A host harness linked from the same source set as
the media APK drives the exported ABI v3 API with multiple simulated station
identities and distinct JPEG sources. It verifies monitor control lifecycles,
concurrent supervised FFmpeg processes, separate RTSP stream paths, process
cleanup, memory guard behavior, and redacted evidence. This is not VM daemon
media injection and does not establish device-side concurrent monitoring.

### 13.4 Field acceptance

Field acceptance records a bidirectional PCAP and system metrics for:

1. one active discovery burst and all matching `07/86` replies;
2. adoption of at least two configured stations;
3. simultaneous `03/04` requests to two stations;
4. independent video traffic and go2rtc streams;
5. an incoming call while the encoder pool is full;
6. CPU, memory, encoder exits, and cleanup after repeated start/stop cycles.

Device-side concurrent monitoring remains unconfirmed until steps 3 and 4
succeed on the actual installation.

## 14. Delivery Order

Implementation proceeds in dependency order:

1. split LuCI pages, add multicast auto/override, and remove media relay UI;
2. add dynamic station registry, discovery sender, candidate cache, and LuCI
   scan/adopt workflow;
3. introduce media ABI v3 and the multi-session encoder manager;
4. expose station and multi-session ubus/HTTP contracts;
5. adapt `doorfastforha` for dynamic station entities and per-station WebRTC;
6. run local, VM, and field acceptance and update the single project status
   document with only verified capability claims.

Each delivery keeps the main and media APK ABI-compatible. The HA change lands
after the main-project HTTP contract is available.

## 15. Acceptance Criteria

The work is complete when:

- LuCI status is read-only and media has a dedicated page;
- host mode supports derived or validated custom multicast;
- users can scan, review, adopt, add, edit, and remove station entries without
  a fixed station-count limit;
- Doorfast exposes only configured stations to HA;
- users can set simultaneous source capacity up to the enabled station count;
- separate stations can own separate encoders and go2rtc streams concurrently;
- incoming-call priority follows the configured policy;
- HA creates and maintains one camera per configured station;
- all local and VM tests pass;
- field documentation distinguishes software support from actual concurrent
  door-station acceptance.
