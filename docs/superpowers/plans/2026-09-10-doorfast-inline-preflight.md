# Doorfast Inline Preflight Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a read-only preflight command that rejects unsafe or incomplete transparent-bridge deployments without changing network state.

**Architecture:** A dedicated deployment configuration names the four interfaces and evidence mount. A pure evaluator consumes a live snapshot collected from UCI, sysfs, `/proc`, mount information, and interface addresses; the CLI prints stable JSON and never repairs a failed check.

**Tech Stack:** C17, Linux sysfs/procfs, `getifaddrs(3)`, `statvfs(3)`, existing Doorfast CLI/test harness, ImmortalWrt UCI text files.

**Spec:** `docs/superpowers/specs/2026-09-10-doorfast-inline-observation-deployment-design.md`

## Global Constraints

- Target x86_64 ImmortalWrt 25.12.1.
- `br-door` has exactly two members and no IPv4/IPv6 address, DHCP, RA, STP, LLDP, or routed firewall membership.
- Management, bridge, upstream, and downstream interface names are explicit and pairwise distinct.
- Doorfast does not create, modify, delete, or restart network interfaces, routes, firewall rules, DHCP, or DNS.
- `passive_only` remains `1`; any other value is a hard failure.
- Evidence root is exactly the independently mounted `/mnt/doorfast`; first provisioning requires at least 30 GiB total and 29 GiB available.
- All failures are reported; none trigger an automatic fallback interface.

---

### Task 1: Parse the deployment configuration

**Files:**
- Create: `src/deployment_config.h`
- Create: `src/deployment_config.c`
- Create: `tests/test_deployment_config.c`
- Create: `tests/fixtures/doorfast-deployment-valid.conf`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: UCI text from `/etc/config/doorfast-deployment`.
- Produces: `int df_deployment_config_parse(const char *, struct df_deployment_config *)` and `int df_deployment_config_validate(const struct df_deployment_config *)`.

- [ ] **Step 1: Write failing parser and validation tests**

Define the public model before the tests:

```c
#define DF_DEPLOYMENT_IFNAME_MAX 64
#define DF_DEPLOYMENT_PATH_MAX 256

struct df_deployment_config {
    bool enabled;
    bool recording_enabled;
    char bridge[DF_DEPLOYMENT_IFNAME_MAX];
    char upstream[DF_DEPLOYMENT_IFNAME_MAX];
    char downstream[DF_DEPLOYMENT_IFNAME_MAX];
    char management[DF_DEPLOYMENT_IFNAME_MAX];
    char evidence_root[DF_DEPLOYMENT_PATH_MAX];
    uint32_t recent_budget_mib;
    uint32_t control_budget_mib;
    uint32_t log_budget_mib;
    uint32_t reserve_mib;
};
```

Add tests which parse these literal values and reject a duplicate interface, a root other than `/mnt/doorfast`, total evidence budgets above 23552 MiB, reserve below 6144 MiB, duplicate options, malformed integers, and an enabled configuration with an empty interface:

```c
void test_deployment_config_parses_confirmed_site_budget(void) {
    struct df_deployment_config c;
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_config_parse(
        "config inline 'main'\n"
        " option enabled '1'\n option recording_enabled '0'\n"
        " option bridge 'br-door'\n option upstream 'door-up'\n"
        " option downstream 'door-down'\n option management 'br-lan'\n"
        " option evidence_root '/mnt/doorfast'\n"
        " option recent_budget_mib '14336'\n"
        " option control_budget_mib '8192'\n"
        " option log_budget_mib '1024'\n option reserve_mib '6144'\n", &c));
    TEST_ASSERT_INT_EQ(14336, c.recent_budget_mib);
    TEST_ASSERT_INT_EQ(8192, c.control_budget_mib);
    TEST_ASSERT_INT_EQ(6144, c.reserve_mib);
}
```

- [ ] **Step 2: Run the test binary and verify RED**

Run: `make test`

Expected: compilation fails because `deployment_config.h` and the two functions do not exist.

- [ ] **Step 3: Implement the strict UCI parser**

Accept one `config inline 'main'` section and exactly the option names shown in the fixture. Use the existing bounded line/quoted-value parsing style from `runtime_config.c`; reject duplicate known options and numeric overflow. Disabled configuration may leave interface names empty, but enabled configuration must satisfy every global constraint expressible from the configuration alone.

- [ ] **Step 4: Run focused and full tests**

Run: `make test`

Expected: all C tests pass, including malformed configuration cases without sanitizer warnings.

- [ ] **Step 5: Commit the configuration model**

```bash
git add Makefile src/deployment_config.c src/deployment_config.h tests/test_deployment_config.c tests/test_main.c tests/fixtures/doorfast-deployment-valid.conf
git commit -m "feat: add inline deployment configuration"
```

### Task 2: Evaluate a deployment snapshot without side effects

**Files:**
- Create: `src/deployment_preflight.h`
- Create: `src/deployment_preflight.c`
- Create: `tests/test_deployment_preflight.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `struct df_deployment_config` and a fully populated `struct df_deployment_snapshot`.
- Produces: `int df_deployment_preflight_evaluate(const struct df_deployment_config *, const struct df_deployment_snapshot *, struct df_deployment_report *)`.

- [ ] **Step 1: Write the failing evaluator tests**

Use explicit result bits so CLI and LuCI can share the contract:

```c
enum df_deployment_failure {
    DF_DEPLOYMENT_MISSING_INTERFACE = 1ULL << 0,
    DF_DEPLOYMENT_BAD_BRIDGE_MEMBERS = 1ULL << 1,
    DF_DEPLOYMENT_BRIDGE_HAS_ADDRESS = 1ULL << 2,
    DF_DEPLOYMENT_BRIDGE_MANAGED = 1ULL << 3,
    DF_DEPLOYMENT_FIREWALL_REFERENCE = 1ULL << 4,
    DF_DEPLOYMENT_DHCP_RA_REFERENCE = 1ULL << 5,
    DF_DEPLOYMENT_STP_ENABLED = 1ULL << 6,
    DF_DEPLOYMENT_MULTICAST_SNOOPING = 1ULL << 7,
    DF_DEPLOYMENT_LLDP_ACTIVE = 1ULL << 8,
    DF_DEPLOYMENT_BAD_MOUNT = 1ULL << 9,
    DF_DEPLOYMENT_LOW_CAPACITY = 1ULL << 10,
    DF_DEPLOYMENT_NOT_PASSIVE = 1ULL << 11
};

struct df_deployment_snapshot {
    bool bridge_exists, upstream_exists, downstream_exists, management_exists;
    char bridge_members[3][DF_DEPLOYMENT_IFNAME_MAX];
    size_t bridge_member_count;
    bool bridge_has_ipv4, bridge_has_ipv6;
    bool network_interface_reference, firewall_reference, dhcp_ra_reference;
    bool stp_enabled, multicast_snooping_enabled, lldp_active;
    bool doorfast_passive_only;
    bool evidence_root_is_mount, evidence_root_is_temporary;
    uint64_t evidence_total_bytes, evidence_available_bytes;
};

struct df_deployment_report {
    uint64_t failures;
    bool safe;
};
```

Build one hand-written passing snapshot and mutate one field per test. Assert that all applicable bits are accumulated in one run rather than returning on the first failure.

- [ ] **Step 2: Run `make test` and verify RED**

Expected: compilation fails because the evaluator and result types are absent.

- [ ] **Step 3: Implement the pure evaluator**

The evaluator must not open files or run commands. It compares the two bridge members without depending on order, verifies the mount and budgets using byte counts, and returns `DF_OK` for a completed evaluation even when `report.safe` is false. Invalid pointers or internally inconsistent snapshots return `DF_ERR_INVALID` without modifying the caller's report.

- [ ] **Step 4: Run normal and sanitizer tests**

Run:

```bash
make test
make clean
make test CC=clang CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support -fsanitize=address,undefined -fno-omit-frame-pointer'
```

Expected: all tests pass; each realistic snapshot mutation sets the corresponding failure bit.

- [ ] **Step 5: Commit the evaluator**

```bash
git add Makefile src/deployment_preflight.c src/deployment_preflight.h tests/test_deployment_preflight.c tests/test_main.c
git commit -m "feat: evaluate inline deployment safety"
```

### Task 3: Collect live Linux and UCI state

**Files:**
- Create: `src/deployment_snapshot.h`
- Create: `src/deployment_snapshot.c`
- Create: `tests/test_deployment_snapshot.c`
- Create: `tests/fixtures/deployment-root/etc/config/network`
- Create: `tests/fixtures/deployment-root/etc/config/firewall`
- Create: `tests/fixtures/deployment-root/etc/config/dhcp`
- Create: `tests/fixtures/deployment-root/proc/self/mountinfo`
- Create fixture sysfs files under: `tests/fixtures/deployment-root/sys/class/net/`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: deployment configuration and a filesystem root used only to redirect reads in tests.
- Produces: `int df_deployment_snapshot_collect(const struct df_deployment_config *, const char *root, struct df_deployment_snapshot *)`.

- [ ] **Step 1: Write fixture-backed failing tests**

The fixture represents `br-door` with `door-up` and `door-down`, STP `0`, multicast snooping `0`, no logical network/firewall/DHCP reference, no bridge addresses, a distinct `br-lan`, and a persistent `/mnt/doorfast`. Add separate fixtures for an extra bridge member, a firewall reference, DHCP/RA reference, and a non-persistent mount.

```c
void test_deployment_snapshot_reads_exact_bridge_members(void) {
    struct df_deployment_config c = confirmed_config();
    struct df_deployment_snapshot s;
    TEST_ASSERT_INT_EQ(DF_OK, df_deployment_snapshot_collect(
        &c, "tests/fixtures/deployment-root", &s));
    TEST_ASSERT_INT_EQ(2, s.bridge_member_count);
    TEST_ASSERT_INT_EQ(0, strcmp("door-up", s.bridge_members[0]));
    TEST_ASSERT_INT_EQ(0, strcmp("door-down", s.bridge_members[1]));
}
```

- [ ] **Step 2: Run `make test` and verify RED**

Expected: compilation fails because snapshot collection does not exist.

- [ ] **Step 3: Implement bounded live collection**

Read bridge membership from `/sys/class/net/<bridge>/brif`, STP and snooping from bridge sysfs, link existence/type from sysfs, and addresses through `getifaddrs()` when `root` is `/`. Parse the three UCI files with a bounded tokenizer and conservatively mark any reference to the bridge or its members. Parse `/proc/self/mountinfo` to prove `/mnt/doorfast` is a distinct non-tmpfs mount, then use `statvfs()` for total and available bytes. Scan `/proc/*/comm` for `lldpd`; if active and the collector cannot prove interface exclusion, set `lldp_active=true`.

Every directory entry and file is length-bounded. Missing evidence creates a failed snapshot field; it never becomes an assumed pass.

- [ ] **Step 4: Run full tests and mutation checks**

Run: `make test`

Expected: valid fixture passes collection; removing a fixture file, changing a bridge member, or changing the mount type causes the relevant snapshot assertion to fail.

- [ ] **Step 5: Commit live collection**

```bash
git add Makefile src/deployment_snapshot.c src/deployment_snapshot.h tests/test_deployment_snapshot.c tests/test_main.c tests/fixtures/deployment-root
git commit -m "feat: collect read-only deployment state"
```

### Task 4: Expose `doorfast --preflight`

**Files:**
- Modify: `src/main.c`
- Create: `src/deployment_report.c`
- Create: `src/deployment_report.h`
- Modify: `tests/test_main_cli.sh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: deployment parser, snapshot collector, and evaluator.
- Produces: `doorfast --preflight /etc/config/doorfast-deployment` with stable JSON and exit codes `0` safe, `2` unsafe/configuration failure, `1` operational read failure.

- [ ] **Step 1: Add a failing CLI test**

Add a test-only root argument available only to host builds:

```sh
output=$(./build/doorfast --preflight tests/fixtures/doorfast-deployment-valid.conf \
  --root tests/fixtures/deployment-root)
test "$(printf '%s' "$output" | jq -r .safe)" = true
test "$(printf '%s' "$output" | jq -r .bridge)" = br-door
```

Also run an unsafe fixture, require exit `2`, and require its JSON `failures` array to contain `bridge_has_address`.

- [ ] **Step 2: Run the CLI test and verify RED**

Run: `sh tests/test_main_cli.sh`

Expected: usage rejects `--preflight`.

- [ ] **Step 3: Implement deterministic JSON reporting**

Serialize only fixed keys and enum-derived failure names; interface values must be JSON-escaped. Production builds reject `--root`. Do not print addresses, packet payloads, credentials, or UCI contents.

- [ ] **Step 4: Verify CLI and regression suites**

Run:

```bash
make test
sh tests/test_main_cli.sh
sh tests/test_package_manifest.sh
```

Expected: all pass and the command performs no writes in the fixture root.

- [ ] **Step 5: Commit the preflight CLI**

```bash
git add Makefile src/main.c src/deployment_report.c src/deployment_report.h tests/test_main_cli.sh
git commit -m "feat: expose read-only deployment preflight"
```

### Task 5: Package a disabled site profile and validate in the VM

**Files:**
- Create: `package/doorfast/files/doorfast-deployment.config`
- Modify: `package/doorfast/Makefile`
- Modify: `tests/test_package_manifest.sh`
- Create: `tests/run_doorfast_vm_preflight.py`
- Modify: `README.md`

**Interfaces:**
- Consumes: installed preflight CLI.
- Produces: an installed but disabled `/etc/config/doorfast-deployment` and a repeatable VM acceptance script.

- [ ] **Step 1: Write failing manifest and VM assertions**

The package fixture contains the confirmed budgets and root but empty interface names and `enabled '0'`. Manifest tests require the file in the APK and verify no init script calls `doorfast --preflight` automatically.

- [ ] **Step 2: Run manifest tests and verify RED**

Run: `sh tests/test_package_manifest.sh`

Expected: failure because the deployment config is absent.

- [ ] **Step 3: Install the disabled profile and bump the package release**

Install with `$(INSTALL_CONF)` so upgrades preserve a user's site mapping. Update README with the safe/unsafe exit contract and explicitly state that preflight never changes network configuration.

- [ ] **Step 4: Build the APK and run VM acceptance**

Run the standard GitHub Actions APK build. Install the artifact in the isolated ImmortalWrt VM, create a fixture-only deployment file that references the VM's existing persistent interfaces without changing them, and run:

```bash
python3 -B tests/run_doorfast_vm_preflight.py /absolute/path/to/vm/ssh.sh
```

Expected: missing `br-door` returns unsafe JSON, the `doorfast` service remains running, interface/link state before and after is byte-for-byte identical, and no new route or firewall rule appears.

- [ ] **Step 5: Commit packaging and documentation**

```bash
git add README.md package/doorfast/Makefile package/doorfast/files/doorfast-deployment.config tests/test_package_manifest.sh tests/run_doorfast_vm_preflight.py
git commit -m "packaging: ship disabled inline preflight profile"
```
