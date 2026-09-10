# Doorfast Inline Field Rollout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a reproducible VM rehearsal and a field checklist for safely placing Doorfast between the VLAN 40 access port and MT8157 while preserving manual rollback.

**Architecture:** The repository supplies read-only inventory and comparison scripts plus operator-reviewed documentation. Network creation and physical rewiring remain explicit manual actions; scripts capture and compare state but never apply UCI, bridge, VLAN, route, firewall, or switch changes.

**Tech Stack:** POSIX shell, Python 3 standard library, ImmortalWrt `ubus`/`uci`/sysfs, existing VM SSH helper and Doorfast test injectors.

**Spec:** `docs/superpowers/specs/2026-09-10-doorfast-inline-observation-deployment-design.md`

## Global Constraints

- Execute after preflight, recorder, and deployment-health plans pass their isolated VM gates.
- The field system uses an iKuai router with eight ports, two currently used, and VLANs 10/20/30/40 on a managed Layer 3 switch.
- Doorfast has one existing VLAN 10 management path and two explicitly selected unused physical interfaces for `br-door`.
- MT8157 is independently powered and has no Wi-Fi or alternate network path during observation.
- `/mnt/doorfast` is a distinct 31.32 GiB mount with approximately 29.71 GiB initially available.
- The operator accepts fail-closed behavior and keeps a cable ready to restore switch-to-MT8157 direct connection.
- No field-deployment script applies or commits live network configuration. The isolated-VM test may create only its exact `*-test` disposable interfaces after proving they do not exist.
- No real GVS send path is enabled; `passive_only=1` is mandatory.

---

### Task 1: Capture a read-only field inventory

**Files:**
- Create: `scripts/doorfast-site-inventory.sh`
- Create: `tests/test_site_inventory.sh`
- Create fixtures under: `tests/fixtures/site-inventory/`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: an explicit output path and local read-only system commands.
- Produces: a root-only directory containing `manifest.json` and bounded command outputs, with no packet payloads or credentials.

- [ ] **Step 1: Write a failing fixture-PATH test**

Put deterministic `ubus`, `uci`, `ip`, `bridge`, `ethtool`, `df`, and `mount` shims first in `PATH`. Run the script into a temporary directory and assert the manifest records interface names, MACs, carrier, addresses, bridge membership, routes, offload flags, mount source/type/size/free, Doorfast version, and `passive_only`. Assert directory mode `0700`, file mode `0600`, and no shim receives `set`, `add`, `delete`, `commit`, `reload`, `restart`, or `apply`.

- [ ] **Step 2: Run the shell test and verify RED**

Run: `sh tests/test_site_inventory.sh`

Expected: the inventory script is missing.

- [ ] **Step 3: Implement fail-closed inventory collection**

Require an absolute output path under `/mnt/doorfast/inventory`; create a timestamped directory with a random suffix. Use fixed arguments, never `eval`. Record SHA-256 for `/etc/config/network`, `firewall`, `dhcp`, `doorfast`, and `doorfast-deployment` without copying their contents. If a required query is unavailable, record its named failure and exit nonzero after completing the remaining reads.

- [ ] **Step 4: Run fixture and manifest tests**

Run:

```bash
sh tests/test_site_inventory.sh
sh tests/test_package_manifest.sh
```

Expected: deterministic fixture output passes and the package test confirms no script contains a mutating command.

- [ ] **Step 5: Commit inventory support**

```bash
git add scripts/doorfast-site-inventory.sh tests/test_site_inventory.sh tests/fixtures/site-inventory tests/test_package_manifest.sh
git commit -m "feat: add read-only site inventory"
```

### Task 2: Compare before and after snapshots

**Files:**
- Create: `scripts/compare-doorfast-site.py`
- Create: `tests/test_compare_doorfast_site.py`

**Interfaces:**
- Consumes: two inventory directories and a JSON file naming management, bridge, upstream, and downstream interfaces.
- Produces: JSON comparison with `safe`, `failures`, and `observations`; exits `0` safe, `2` unsafe, `1` unreadable input.

- [ ] **Step 1: Write failing literal comparison tests**

Create complete before/after fixtures. Assert unchanged default route and firewall/UCI hashes pass; a management-route change, new bridge address, extra bridge member, missing carrier, growing link errors, `passive_only=0`, or less than 6 GiB free sets a literal failure name. Packet counters may increase and must appear under observations rather than failures.

- [ ] **Step 2: Run unit tests and verify RED**

Run: `python3 -m unittest tests/test_compare_doorfast_site.py`

Expected: comparison module is missing.

- [ ] **Step 3: Implement deterministic comparison**

Parse JSON with the standard library, require schema version 1 and every mandatory key, and compare integers without float conversion. Print sorted keys and failure arrays. Never invoke a system command from Python.

- [ ] **Step 4: Run all comparison cases**

Run: `python3 -m unittest tests/test_compare_doorfast_site.py`

Expected: all safe and unsafe fixtures produce the documented exit codes.

- [ ] **Step 5: Commit comparison support**

```bash
git add scripts/compare-doorfast-site.py tests/test_compare_doorfast_site.py
git commit -m "feat: compare inline deployment snapshots"
```

### Task 3: Rehearse an isolated inline bridge in the VM

**Files:**
- Create: `tests/run_doorfast_vm_inline_bridge.py`
- Modify: `docs/gvs-vm-udp-validation.md`

**Interfaces:**
- Consumes: VM SSH helper, installed APKs, two disposable veth pairs, and an isolated bridge name `br-door-test`.
- Produces: a VM-only proof that forwarding survives parser/recorder failure and preflight/status identify the expected topology.

- [ ] **Step 1: Write the VM script and run it against the previous APK**

The script first verifies that all names end in `-test` and are absent, then creates disposable veth pairs and `br-door-test` inside the isolated VM. It sends a synthetic Ethernet/UDP fixture between test endpoints, verifies both directions, runs preflight, starts Doorfast and recorder, kills each process separately, and verifies forwarding after each kill. A `finally` block removes only the four exact test interfaces and test bridge.

Expected against the previous APK: RED because preflight and recorder commands are absent.

- [ ] **Step 2: Add required VM-only test dependencies**

Document `ip-full` and `kmod-veth` as test-image packages, not Doorfast runtime dependencies. Do not run this QEMU test in GitHub Actions; Actions continues to build APKs and host tests only.

- [ ] **Step 3: Run the script against the new APK**

Run:

```bash
python3 -B tests/run_doorfast_vm_inline_bridge.py /absolute/path/to/vm/ssh.sh
```

Expected: preflight safe, bidirectional fixture delivery, evidence segments created, ubus health changes visible, and forwarding remains intact after both observer processes are stopped.

- [ ] **Step 4: Commit VM rehearsal**

```bash
git add tests/run_doorfast_vm_inline_bridge.py docs/gvs-vm-udp-validation.md
git commit -m "test: rehearse isolated inline bridge"
```

### Task 4: Write the operator-run field procedure

**Files:**
- Create: `docs/doorfast-inline-field-deployment.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: confirmed design, preflight command, inventory/compare scripts, recorder, and LuCI deployment health.
- Produces: a five-stage manual procedure with explicit stop/go and rollback gates.

- [ ] **Step 1: Write the procedure with exact gates**

The document must include:

1. record interface/MAC/panel mapping and produce the pre-change inventory;
2. back up ImmortalWrt, iKuai, and switch configurations outside `/mnt/doorfast`;
3. prepare and label the direct switch-to-MT8157 rollback cable;
4. require a maintenance window before any physical rewire;
5. configure the switch port as VLAN 40 untagged access through its existing administrative interface;
6. have the operator manually create an unnumbered two-member bridge, with no reusable command containing site-specific interface guesses;
7. keep Doorfast and recorder disabled for the first functional test;
8. test MT8157 call, open-door, elevator, and online behavior;
9. run short captures on bridge/upstream/downstream to choose exactly one observation interface;
10. enable bounded recording, force rotation/space guard, then enable Doorfast passive parsing;
11. require seven days without Doorfast-caused interruption before evidence review;
12. stop observers first on anomaly, then physically restore switch-to-MT8157 direct wiring if the anomaly remains.

- [ ] **Step 2: Self-check the document against the design acceptance list**

For each of the ten acceptance criteria in the design, add one checklist row naming the evidence file or human observation that proves it. Remove all example interface names that could be pasted without verification, except the fixed bridge name `br-door`.

- [ ] **Step 3: Link the procedure from README**

State prominently that the APK never creates the bridge and that software bridge failure disconnects MT8157 until manual bypass.

- [ ] **Step 4: Commit field documentation**

```bash
git add README.md docs/doorfast-inline-field-deployment.md
git commit -m "docs: add inline field rollout procedure"
```

### Task 5: Final release gate

**Files:**
- Modify: `docs/doorfast-inline-field-deployment.md`

**Interfaces:**
- Consumes: all prior test outputs and a successful x86_64 APK build.
- Produces: a release checklist that distinguishes software readiness from site authorization.

- [ ] **Step 1: Run complete local verification**

Run:

```bash
make test
python3 -m unittest tests/test_gvs_peer_udp.py tests/test_compare_doorfast_site.py
sh tests/test_main_cli.sh
sh tests/test_site_inventory.sh
sh tests/test_package_manifest.sh
node tests/test_luci_status.js
```

Expected: every command exits zero.

- [ ] **Step 2: Build APKs and run all isolated VM checks**

After GitHub Actions completes successfully, install the artifact and run the preflight, recorder, deployment-status, inline-bridge, UDP, and ubus-call VM scripts. Do not continuously poll Actions.

Expected: all scripts print `PASS`; no VM route, firewall, or management-interface change persists.

- [ ] **Step 3: Record the boundary and commit**

Add artifact hashes and test results to the deployment document. Mark software `ready for operator-reviewed field stage 0`; do not mark the physical deployment complete until the user supplies verified interface mapping and explicitly schedules the maintenance window.

```bash
git add docs/doorfast-inline-field-deployment.md
git commit -m "docs: record inline deployment release gate"
```
