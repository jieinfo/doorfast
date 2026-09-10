# Doorfast Deployment Health and LuCI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose read-only inline-link, observation, storage, and recorder health through ubus and LuCI without adding control buttons or active probes.

**Architecture:** A small health collector reads explicit interface counters, the recorder's atomic status file, deployment configuration, preflight result, and the Doorfast capture timestamp. The existing `doorfast status` response gains a versioned `deployment` table, and LuCI renders it using the existing five-second polling path.

**Tech Stack:** C17, sysfs, `statvfs(3)`, existing libubus/libubox integration, LuCI JavaScript, Node.js unit tests.

**Spec:** `docs/superpowers/specs/2026-09-10-doorfast-inline-observation-deployment-design.md`

## Global Constraints

- Execute after the inline preflight and evidence recorder plans.
- All collection is read-only; no ping, ARP, GVS probe, bridge mutation, service restart, or filesystem cleanup.
- Exact interface names and local device identifiers may be shown only to authenticated VLAN 10 administrators.
- Raw PCAP and packet payloads are never returned through ubus or LuCI.
- LuCI remains status-only and has no answer, hangup, open-door, elevator, network-apply, or capture-download button.
- Missing recorder or unavailable counters produce explicit degraded state, not fabricated zeros.

---

### Task 1: Collect deployment health into a stable model

**Files:**
- Create: `src/deployment_health.h`
- Create: `src/deployment_health.c`
- Create: `tests/test_deployment_health.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`
- Modify: `src/runtime_service.h`
- Modify: `src/runtime_service.c`

**Interfaces:**
- Consumes: deployment config, preflight report, sysfs root, recorder status path, mount path, and last capture timestamps.
- Produces: `int df_deployment_health_collect(const struct df_deployment_health_request *, struct df_deployment_health *)`.

- [ ] **Step 1: Write failing health-model tests**

Define explicit presence flags rather than representing missing data as zero:

```c
struct df_link_health {
    char name[DF_DEPLOYMENT_IFNAME_MAX];
    bool present;
    bool carrier_known;
    bool carrier;
    bool counters_known;
    uint64_t rx_packets, tx_packets, rx_dropped, tx_dropped;
    uint64_t rx_errors, tx_errors;
};

struct df_deployment_health {
    uint32_t schema_version;
    bool configured;
    bool preflight_safe;
    bool passive_only;
    char observation_interface[DF_DEPLOYMENT_IFNAME_MAX];
    struct df_link_health upstream, downstream, management;
    bool capture_seen;
    uint64_t last_capture_wall_seconds;
    bool recorder_present;
    char recorder_state[24];
    uint64_t recent_bytes, control_bytes, log_bytes;
    uint64_t available_bytes, reserve_bytes;
};
```

Use fixture sysfs and recorder status files. Test link-up, link-down, missing counters, malformed status, missing recorder, exact 64-bit counters, and free space below reserve.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: health types and collector are missing.

- [ ] **Step 3: Implement bounded read-only collection**

Read only the three configured interface directories and fixed recorder keys. Reject duplicate keys, overlong lines, negative values, and numeric overflow. Set `recorder_present=false` for a missing status file; return `DF_OK` with degraded fields. Return `DF_ERR_INVALID` only for invalid arguments and `DF_ERR_IO` when core deployment configuration cannot be read.

Update runtime capture bookkeeping only after `DF_CAPTURE_PACKET`, storing wall and monotonic timestamps without packet bytes.

- [ ] **Step 4: Run normal and sanitizer tests**

Run:

```bash
make test
make clean
make test CC=clang CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Expected: all health and existing runtime tests pass.

- [ ] **Step 5: Commit the health model**

```bash
git add Makefile src/deployment_health.c src/deployment_health.h src/runtime_service.c src/runtime_service.h tests/test_deployment_health.c tests/test_main.c
git commit -m "feat: collect inline deployment health"
```

### Task 2: Add deployment health to ubus status

**Files:**
- Modify: `src/runtime_ubus.h`
- Modify: `src/runtime_ubus.c`
- Modify: `src/runtime_service.c`
- Modify: `tests/test_runtime_ubus.c`

**Interfaces:**
- Consumes: `df_runtime_deployment_status_provider_fn` returning `struct df_deployment_health`.
- Produces: `int df_runtime_ubus_bind_deployment(...)` and a `deployment` object in `ubus call doorfast status`.

```c
typedef int (*df_runtime_deployment_status_provider_fn)(
    struct df_deployment_health *status, void *context);

int df_runtime_ubus_bind_deployment(
    struct df_runtime_ubus *service,
    df_runtime_deployment_status_provider_fn provide_status,
    void *context);
```

- [ ] **Step 1: Write failing binding and status tests**

Add a provider fixture with carrier up/down, exact counters above `UINT32_MAX`, recorder `space_guard`, and known byte budgets. Assert binding is single-assignment and status retrieval preserves every 64-bit value.

The public JSON contract is:

```json
{
  "deployment": {
    "schema_version": 1,
    "configured": true,
    "preflight_safe": true,
    "passive_only": true,
    "observation_interface": "br-door",
    "upstream": {"name":"door-up","present":true,"carrier":true,"rx_packets":1,"tx_packets":2,"rx_dropped":0,"tx_dropped":0,"rx_errors":0,"tx_errors":0},
    "downstream": {"name":"door-down","present":true,"carrier":true,"rx_packets":3,"tx_packets":4,"rx_dropped":0,"tx_dropped":0,"rx_errors":0,"tx_errors":0},
    "management": {"name":"br-lan","present":true,"carrier":true},
    "capture": {"seen":true,"last_wall_seconds":1},
    "recorder": {"present":true,"state":"space_guard","recent_bytes":0,"control_bytes":0,"log_bytes":0,"available_bytes":6442450944,"reserve_bytes":6442450944}
  }
}
```

When carrier or counters are unknown, omit that key rather than emit false or zero.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: binding and deployment status provider do not exist.

- [ ] **Step 3: Implement provider binding and ubus serialization**

Follow the existing call-status binding pattern. Add nested tables through libubox, use `blobmsg_add_u64` for counters and bytes, and JSON-escape strings through blobmsg. Failure to collect deployment health must leave existing service/sync/call status available with `deployment.configured=false` rather than fail the entire method.

- [ ] **Step 4: Run host tests and target compile checks**

Run:

```bash
make test
sh tests/test_package_manifest.sh
```

Expected: host tests pass. The subsequent GitHub SDK build must compile the `DF_WITH_UBUS` path before merge.

- [ ] **Step 5: Commit ubus deployment status**

```bash
git add src/runtime_ubus.c src/runtime_ubus.h src/runtime_service.c tests/test_runtime_ubus.c
git commit -m "feat: expose deployment health over ubus"
```

### Task 3: Render deployment health in LuCI

**Files:**
- Modify: `package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js`
- Modify: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js`
- Modify: `tests/test_luci_status.js`
- Modify: `package/luci-app-doorfast/Makefile`

**Interfaces:**
- Consumes: version 1 `deployment` JSON from Task 2.
- Produces: status sections `透明链路`, `观察`, and `本地证据` using existing `statusModel.formatStatus()`.

- [ ] **Step 1: Write failing LuCI model tests**

Extend the complete payload fixture rather than adding a partial mock. Assert literal labels for both carriers, observation interface, last capture time, recorder state, recent/control/log usage, free space, and reserve. Add cases for missing recorder, unknown counter, unsafe preflight, and schema version 2.

```javascript
assert.deepStrictEqual(section.rows[0], ['预检', '安全']);
assert.deepStrictEqual(section.rows[1], ['被动模式', '是']);
assert.deepStrictEqual(section.rows[2], ['门禁上联', 'door-up · 链路正常']);
```

- [ ] **Step 2: Run Node tests and verify RED**

Run: `node tests/test_luci_status.js`

Expected: deployment sections are missing.

- [ ] **Step 3: Implement strict rendering**

Require `schema_version === 1`; treat absent optional carrier/counter keys as `未知`; reject malformed numeric values. Format byte values into GiB with one decimal place using integer-safe JavaScript arithmetic. Preserve the existing five-second poll and stale-data warning. Do not add RPC methods or buttons.

- [ ] **Step 4: Run JavaScript and manifest tests**

Run:

```bash
node tests/test_luci_status.js
sh tests/test_package_manifest.sh
```

Expected: all status sections render and the package still contains only read-only UI behavior.

- [ ] **Step 5: Commit LuCI status**

```bash
git add package/luci-app-doorfast/Makefile package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js tests/test_luci_status.js
git commit -m "feat: display inline deployment health"
```

### Task 4: Validate live refresh and ACL in the VM

**Files:**
- Create: `tests/run_doorfast_vm_deployment_status.py`
- Modify: `docs/doorfast-ubus-luci-status.md`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: built core and LuCI APKs, authenticated HTTP ubus, VM SSH helper, fixture deployment state.
- Produces: repeatable proof that real rpcd ACL and LuCI polling observe health changes.

- [ ] **Step 1: Write the acceptance script against the old APK**

The script logs into `http://127.0.0.1:8080/ubus`, verifies `session access` allows only `doorfast.status`, reads a baseline, changes a fixture carrier/status file in the isolated test environment, waits for a new status value, and confirms the served LuCI asset contains the five-second polling path and no control handler.

- [ ] **Step 2: Run against the old APK and verify RED**

Expected: the `deployment` table is absent.

- [ ] **Step 3: Build and install both APKs**

Use the successful GitHub Actions x86_64 artifacts. Bump `doorfast` and `luci-app-doorfast` releases. Do not continuously poll the remote build.

- [ ] **Step 4: Run VM acceptance**

Run:

```bash
python3 -B tests/run_doorfast_vm_deployment_status.py /absolute/path/to/vm/ssh.sh
```

Expected: authenticated HTTP status changes are visible, unauthorized methods remain denied, raw evidence is inaccessible through HTTP, and no route/firewall/bridge state changes.

- [ ] **Step 5: Commit acceptance evidence**

```bash
git add docs/doorfast-ubus-luci-status.md tests/run_doorfast_vm_deployment_status.py tests/test_package_manifest.sh
git commit -m "test: validate deployment status refresh"
```
