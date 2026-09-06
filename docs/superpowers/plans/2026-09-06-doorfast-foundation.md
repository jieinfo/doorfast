# Doorfast foundation implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native x86_64 ImmortalWrt daemon that loads Doorfast UCI configuration, transparently captures selected Dnake SIP traffic, emits normalized call events and administrator-reviewed discovery candidates, applies schedule policies, and installs as an IPK without any license or remote-shell behavior.

**Architecture:** A C17 daemon is split into configuration, capture, SIP parsing, session/policy, discovery, diagnostics, integration, and audit modules. The first increment is deliberately transparent and passive: it produces normalized events, reviewed endpoint candidates, and scheduled action decisions but does not transmit Dnake door/elevator control frames until user-owned packet fixtures document those frames.

**Tech Stack:** C17, libuci, libpcap, json-c, OpenSSL, procd, LuCI, ImmortalWrt SDK, host `make` test runner.

**Spec:** `docs/superpowers/specs/2026-09-06-doorfast-design.md`

## Global constraints

- Build with the matching x86_64 ImmortalWrt 25.12.1 SDK.
- Use C17 and the target SDK's libpcap, libuci, json-c, and OpenSSL.
- Support Dnake only in this release line.
- Do not contain licensing, activation, vendor-cloud deployment, or network-downloaded shell execution.
- Do not expose a local unauthenticated HTTP server.
- Keep active door, hangup, and elevator transmission disabled until user-owned PCAP fixtures define each wire interaction.
- Redact tokens, passwords, SIP credentials, and raw packet bodies from all logs.
- Treat transparent mode as the only supported first-release mode; host mode and video are separately gated roadmap work.
- Discovery may create reviewable candidates but must never add active targets without administrator confirmation.
- Diagnostics must be read-only: never rewrite UCI network configuration, routes, firewall rules, or interface addresses.

---

## File structure

| Path | Responsibility |
|---|---|
| `src/doorfast.h` | Common constants and process return codes |
| `src/config.h`, `src/config.c` | UCI-independent configuration model and validation |
| `src/event.h`, `src/event.c` | Normalized event types and safe serialization |
| `src/sip.h`, `src/sip.c` | Bounded SIP start-line/header parser |
| `src/session.h`, `src/session.c` | Call-ID session table and transitions |
| `src/policy.h`, `src/policy.c` | DND/schedule/action decision engine |
| `src/capture.h`, `src/capture.c` | libpcap device/filter/open loop wrapper |
| `src/audit.h`, `src/audit.c` | syslog and file audit records with redaction |
| `src/discovery.h`, `src/discovery.c` | Endpoint candidate parsing, deduplication, and approval boundary |
| `src/diagnostics.h`, `src/diagnostics.c` | Read-only overlap and reachability diagnostics |
| `src/main.c` | CLI, lifecycle, signal handling, module wiring |
| `tests/test.h`, `tests/test_main.c` | Minimal host-side C test framework |
| `tests/test_config.c` | Validation and secret-redaction tests |
| `tests/test_sip.c` | SIP parser fixture tests |
| `tests/test_policy.c` | Time and automation policy tests |
| `tests/test_discovery.c` | Candidate parsing, deduplication, and approval tests |
| `tests/test_diagnostics.c` | Read-only diagnostic-result tests |
| `tests/fixtures/*.sip` | Anonymized SIP request/response fixtures |
| `package/doorfast/Makefile` | ImmortalWrt package recipe |
| `package/doorfast/files/doorfast.init` | procd service definition |
| `package/doorfast/files/doorfast.config` | Default UCI configuration |
| `luci-app-doorfast/luasrc/*` | LuCI configuration page and ACL |
| `Makefile` | Host build and test targets |

## Task 1: Create the host build and test harness

**Files:**
- Create: `Makefile`
- Create: `src/doorfast.h`
- Create: `tests/test.h`
- Create: `tests/test_main.c`
- Test: `tests/test_main.c`

**Interfaces:**
- Produces: `make test`, which builds and runs `build/doorfast-tests`.
- Produces: `DF_ARRAY_LEN(array)` and return codes `DF_OK`, `DF_ERR_INVALID`, `DF_ERR_IO`.

- [ ] **Step 1: Write the failing test harness smoke test**

```c
/* tests/test_main.c */
#include "test.h"

int test_suite_count(void) { return 0; }

int main(void) {
    TEST_ASSERT_INT_EQ(0, test_suite_count());
    return test_failures();
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make test`

Expected: FAIL because the Makefile and `test.h` do not exist.

- [ ] **Step 3: Implement the minimal harness and build target**

```make
CC ?= cc
CFLAGS := -std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests
TEST_SOURCES := tests/test_main.c

test: build/doorfast-tests

build/doorfast-tests: $(TEST_SOURCES) tests/test.h src/doorfast.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(TEST_SOURCES) -o $@
	$@
```

```c
/* tests/test.h */
#ifndef DOORFAST_TEST_H
#define DOORFAST_TEST_H
#include <stdio.h>
static int df_test_failures = 0;
#define TEST_ASSERT_INT_EQ(expected, actual) do { \
  if ((expected) != (actual)) { \
    fprintf(stderr, "%s:%d expected %d got %d\\n", __FILE__, __LINE__, (expected), (actual)); \
    df_test_failures++; \
  } \
} while (0)
static inline int test_failures(void) { return df_test_failures; }
#endif
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `make test`

Expected: PASS with exit code 0.

- [ ] **Step 5: Commit**

```bash
git add Makefile src/doorfast.h tests/test.h tests/test_main.c
git commit -m "build: add Doorfast test harness"
```

## Task 2: Implement configuration validation and redaction

**Files:**
- Create: `src/config.h`
- Create: `src/config.c`
- Create: `tests/test_config.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Produces: `int df_config_validate(const struct df_config *config)`.
- Produces: `void df_config_redact(char *dst, size_t dst_size, const char *secret)`.
- Consumes: `DF_OK` and `DF_ERR_INVALID` from `src/doorfast.h`.

- [ ] **Step 1: Write failing validation and redaction tests**

```c
/* tests/test_config.c */
#include "config.h"
#include "test.h"

void test_config_validation(void) {
  struct df_config valid = {.enabled = true, .brand = "dnake", .capture_interface = "br-door"};
  struct df_config invalid = {.enabled = true, .brand = "other", .capture_interface = ""};
  TEST_ASSERT_INT_EQ(DF_OK, df_config_validate(&valid));
  TEST_ASSERT_INT_EQ(DF_ERR_INVALID, df_config_validate(&invalid));
}

void test_config_redaction(void) {
  char output[32];
  df_config_redact(output, sizeof(output), "abcdefghijk");
  TEST_ASSERT_INT_EQ(0, strcmp("abcd…", output));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make test`

Expected: FAIL because `config.h`, `df_config_validate`, and `df_config_redact` do not exist.

- [ ] **Step 3: Implement the configuration model**

```c
struct df_config {
  bool enabled;
  const char *brand;
  const char *capture_interface;
  bool capture_auto;
  bool capture_promiscuous;
  int unlock_delay_seconds;
  int hangup_delay_seconds;
  bool call_elev;
};

int df_config_validate(const struct df_config *config) {
  if (config == NULL || !config->enabled) return DF_OK;
  if (strcmp(config->brand, "dnake") != 0) return DF_ERR_INVALID;
  if (!config->capture_auto && config->capture_interface[0] == '\0') return DF_ERR_INVALID;
  if (config->unlock_delay_seconds < -1 || config->unlock_delay_seconds > 9) return DF_ERR_INVALID;
  if (config->hangup_delay_seconds < -1 || config->hangup_delay_seconds > 9) return DF_ERR_INVALID;
  return DF_OK;
}
```

`df_config_redact` returns `""` for an empty secret and the first four bytes plus UTF-8 ellipsis for a non-empty secret; it never copies more than `dst_size - 1` bytes.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make test`

Expected: PASS with validation and redaction assertions succeeding.

- [ ] **Step 5: Commit**

```bash
git add Makefile src/config.h src/config.c tests/test_config.c tests/test_main.c
git commit -m "feat: add validated Doorfast configuration"
```

## Task 3: Parse SIP messages into bounded normalized events

**Files:**
- Create: `src/event.h`
- Create: `src/event.c`
- Create: `src/sip.h`
- Create: `src/sip.c`
- Create: `tests/fixtures/invite.sip`
- Create: `tests/fixtures/bye.sip`
- Create: `tests/test_sip.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Produces: `enum df_event_type { DF_EVENT_INCOMING_CALL, DF_EVENT_HANGUP, DF_EVENT_UNKNOWN }`.
- Produces: `int df_sip_parse(const uint8_t *data, size_t length, struct df_event *event)`.
- Consumes: an immutable UDP/TCP payload and returns no raw secret-bearing payload.

- [ ] **Step 1: Write failing SIP fixture tests**

```c
void test_invite_parses_as_incoming_call(void) {
  struct df_event event = {0};
  const char packet[] = "INVITE sip:1001@172.16.10.2 SIP/2.0\r\nCall-ID: call-a\r\n\r\n";
  TEST_ASSERT_INT_EQ(DF_OK, df_sip_parse((const uint8_t *)packet, strlen(packet), &event));
  TEST_ASSERT_INT_EQ(DF_EVENT_INCOMING_CALL, event.type);
  TEST_ASSERT_INT_EQ(0, strcmp("call-a", event.call_id));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make test`

Expected: FAIL because SIP and event interfaces do not exist.

- [ ] **Step 3: Implement bounded parser behavior**

Implement `df_sip_parse` to accept only a payload no longer than 65,535 bytes, locate the first CRLF-delimited start line, detect `INVITE` and `BYE`, and extract a `Call-ID` value no longer than 127 bytes. Missing/oversized fields return `DF_ERR_INVALID`; syntactically valid unsupported methods return `DF_EVENT_UNKNOWN`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make test`

Expected: PASS; INVITE maps to `DF_EVENT_INCOMING_CALL`, BYE maps to `DF_EVENT_HANGUP`, malformed messages return `DF_ERR_INVALID`.

- [ ] **Step 5: Commit**

```bash
git add Makefile src/event.h src/event.c src/sip.h src/sip.c tests/fixtures tests/test_sip.c tests/test_main.c
git commit -m "feat: parse normalized SIP call events"
```

## Task 4: Add session state and time-policy decisions

**Files:**
- Create: `src/session.h`
- Create: `src/session.c`
- Create: `src/policy.h`
- Create: `src/policy.c`
- Create: `tests/test_policy.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Produces: `int df_session_apply(struct df_session *session, const struct df_event *event)`.
- Produces: `enum df_decision df_policy_decide(const struct df_policy *policy, const struct df_event *event, time_t now)`.
- Produces: `DF_DECISION_NOTIFY`, `DF_DECISION_HANGUP`, `DF_DECISION_OPEN_AFTER_DELAY`, and `DF_DECISION_IGNORE`.

- [ ] **Step 1: Write failing policy tests**

```c
void test_dnd_hangup_wins_over_unlock(void) {
  struct df_policy policy = {.dnd_active = true, .unlock_delay_seconds = 0, .hangup_delay_seconds = 0};
  struct df_event event = {.type = DF_EVENT_INCOMING_CALL};
  TEST_ASSERT_INT_EQ(DF_DECISION_HANGUP, df_policy_decide(&policy, &event, 0));
}

void test_disabled_unlock_only_notifies(void) {
  struct df_policy policy = {.dnd_active = false, .unlock_delay_seconds = -1, .hangup_delay_seconds = -1};
  struct df_event event = {.type = DF_EVENT_INCOMING_CALL};
  TEST_ASSERT_INT_EQ(DF_DECISION_NOTIFY, df_policy_decide(&policy, &event, 0));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make test`

Expected: FAIL because session and policy interfaces do not exist.

- [ ] **Step 3: Implement deterministic policy ordering**

`df_policy_decide` must return `DF_DECISION_IGNORE` for non-call events, apply DND before auto-unlock, return `DF_DECISION_OPEN_AFTER_DELAY` only for delay values 0 through 9 within an enabled schedule, and return `DF_DECISION_NOTIFY` when no active action is allowed. `df_session_apply` rejects an event with a different Call-ID from the active session.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make test`

Expected: PASS with DND precedence and disabled automation behavior verified.

- [ ] **Step 5: Commit**

```bash
git add Makefile src/session.h src/session.c src/policy.h src/policy.c tests/test_policy.c tests/test_main.c
git commit -m "feat: add session and automation policy engine"
```

## Task 5: Wrap libpcap and audit output without active controls

**Files:**
- Create: `src/capture.h`
- Create: `src/capture.c`
- Create: `src/audit.h`
- Create: `src/audit.c`
- Create: `src/main.c`
- Modify: `Makefile`
- Test: `tests/test_sip.c`

**Interfaces:**
- Produces: `int df_capture_open(const char *device, bool promiscuous, struct df_capture **capture)`.
- Produces: `int df_capture_set_filter(struct df_capture *capture, const char *bpf)`.
- Produces: `void df_audit_event(const struct df_event *event, enum df_decision decision)`.
- Consumes: parser and policy interfaces from Tasks 3 and 4.

- [ ] **Step 1: Add a failing test for the default BPF expression**

```c
void test_default_filter_excludes_unrelated_traffic(void) {
  TEST_ASSERT_INT_EQ(0, strcmp("udp port 5060 or tcp port 5060", df_capture_default_filter()));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make test`

Expected: FAIL because `df_capture_default_filter` does not exist.

- [ ] **Step 3: Implement capture and audit wiring**

Implement `df_capture_default_filter` with the exact string in the test. `df_capture_open` uses `pcap_create`, `pcap_set_snaplen(2048)`, `pcap_set_timeout(1000)`, `pcap_set_promisc`, and `pcap_activate`; any libpcap error returns `DF_ERR_IO` with no retry loop. `main` loads configuration, opens the selected capture device, parses each callback payload, evaluates policy, and calls `df_audit_event`. It logs decisions only; it does not send a Dnake control frame.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make test`

Expected: PASS. Then run `make doorfast` and verify `./build/doorfast --help` exits 0 without opening a capture device.

- [ ] **Step 5: Commit**

```bash
git add Makefile src/capture.h src/capture.c src/audit.h src/audit.c src/main.c tests/test_sip.c
git commit -m "feat: add passive SIP capture daemon"
```

## Task 6: Package the passive daemon and LuCI configuration

**Files:**
- Create: `package/doorfast/Makefile`
- Create: `package/doorfast/files/doorfast.init`
- Create: `package/doorfast/files/doorfast.config`
- Create: `luci-app-doorfast/luasrc/controller/doorfast.lua`
- Create: `luci-app-doorfast/luasrc/model/cbi/doorfast.lua`
- Create: `luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json`
- Modify: `README.md`

**Interfaces:**
- Produces: an x86_64 IPK named `doorfast` and a LuCI page at `admin/services/doorfast`.
- Consumes: `/usr/sbin/doorfast` and `/etc/config/doorfast`.

- [ ] **Step 1: Add a package manifest test**

```sh
test -f package/doorfast/Makefile
test -f package/doorfast/files/doorfast.init
test -f package/doorfast/files/doorfast.config
rg -q 'PKGARCH:=x86_64' package/doorfast/Makefile
! rg -q 'wget -O-|auth|auto_update' package/doorfast/files/doorfast.config
```

- [ ] **Step 2: Run the manifest test to verify it fails**

Run: `sh tests/test_package_manifest.sh`

Expected: FAIL because the package files do not exist.

- [ ] **Step 3: Implement the package and LuCI assets**

The package recipe depends on `+libpcap +libuci +libjson-c +libopenssl`. The init script uses `USE_PROCD=1`, launches `/usr/sbin/doorfast --config /etc/config/doorfast`, and sets `respawn 3600 5 5`. The default UCI file sets `brand 'dnake'`, `enabled '0'`, `capture_auto '1'`, `capture_promiscuous '0'`, `unlock '-1'`, `hangup '-1'`, and `call_elev '0'`. The LuCI page exposes the fields documented in the spec and does not create an online-update button in this passive release.

- [ ] **Step 4: Run the manifest and host tests to verify they pass**

Run: `sh tests/test_package_manifest.sh && make test`

Expected: PASS. In the matching SDK, run `make package/doorfast/compile V=s` and verify that an x86_64 IPK is emitted.

- [ ] **Step 5: Commit**

```bash
git add package/doorfast luci-app-doorfast README.md tests/test_package_manifest.sh
git commit -m "feat: package passive Doorfast for ImmortalWrt"
```

## Task 7: Add administrator-reviewed Dnake endpoint discovery

**Files:**
- Create: `src/discovery.h`
- Create: `src/discovery.c`
- Create: `tests/test_discovery.c`
- Modify: `src/event.h`
- Modify: `src/config.h`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Produces: `int df_endpoint_parse(const char *text, struct df_endpoint *endpoint)`.
- Produces: `enum df_discovery_result df_discovery_observe(const struct df_event *event, struct df_candidate_store *store)`.
- Produces: `int df_candidate_approve(const struct df_candidate *candidate, struct df_config *config)`.
- Consumes: parsed events from Task 3 and validated configuration from Task 2.

- [ ] **Step 1: Write failing endpoint and candidate tests**

```c
void test_dnake_endpoint_is_structured(void) {
  struct df_endpoint endpoint = {0};
  TEST_ASSERT_INT_EQ(DF_OK, df_endpoint_parse("10019901:secret@172.16.1.101:5060", &endpoint));
  TEST_ASSERT_INT_EQ(0, strcmp("10019901", endpoint.id));
  TEST_ASSERT_INT_EQ(5060, endpoint.port);
}

void test_observed_endpoint_requires_approval(void) {
  struct df_candidate_store store = {0};
  struct df_event event = {.type = DF_EVENT_INCOMING_CALL, .remote_host = "172.16.1.101", .remote_port = 5060};
  TEST_ASSERT_INT_EQ(DF_DISCOVERY_CANDIDATE, df_discovery_observe(&event, &store));
  TEST_ASSERT_INT_EQ(0, store.candidates[0].approved);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make test`

Expected: FAIL because discovery interfaces do not exist.

- [ ] **Step 3: Implement structured parsing and approval boundary**

Implement `df_endpoint_parse` for the documented `ID[:credential]@host:port`
import notation. Enforce bounded lengths, a numeric port from 1 through 65535,
and reject malformed IPv4/IPv6 host syntax. `df_discovery_observe` must
deduplicate candidates by normalized host, port, and ID. `df_candidate_approve`
is the sole path that transfers a candidate into the active endpoint allowlist;
capture and automation paths must ignore unapproved candidates.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make test`

Expected: PASS; malformed entries are rejected, duplicate observations do not
create a second candidate, and unapproved candidates cannot become targets.

- [ ] **Step 5: Commit**

```bash
git add Makefile src/discovery.h src/discovery.c src/event.h src/config.h tests/test_discovery.c tests/test_main.c
git commit -m "feat: add reviewed Dnake endpoint discovery"
```

## Task 8: Add read-only network diagnostics and integration configuration

**Files:**
- Create: `src/diagnostics.h`
- Create: `src/diagnostics.c`
- Create: `tests/test_diagnostics.c`
- Modify: `src/config.h`
- Modify: `luci-app-doorfast/luasrc/model/cbi/doorfast.lua`
- Modify: `README.md`

**Interfaces:**
- Produces: `enum df_overlap_state df_diagnostics_overlap(const struct df_network_info *doorfast, const struct df_network_info *entry_network)`.
- Produces: `int df_diagnostics_probe(const struct df_endpoint *endpoint, struct df_probe_result *result)`.
- Consumes: administrator-configured networks and approved endpoints only.

- [ ] **Step 1: Write failing diagnostic tests**

```c
void test_overlapping_networks_warn_without_mutation(void) {
  struct df_network_info a = {.address = "192.168.5.1", .prefix_length = 24};
  struct df_network_info b = {.address = "192.168.5.20", .prefix_length = 24};
  TEST_ASSERT_INT_EQ(DF_OVERLAP_WARNING, df_diagnostics_overlap(&a, &b));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make test`

Expected: FAIL because diagnostic interfaces do not exist.

- [ ] **Step 3: Implement no-mutation diagnostics and LuCI presentation**

Implement overlap evaluation and bounded TCP/UDP reachability probes with a
short timeout. Return observations and remediation guidance only; do not call
UCI setters, `ip route`, firewall commands, or shell helpers. LuCI displays
capture-interface status, overlap warnings, approved endpoint probe results,
and configuration hints. It also exposes HA HTTPS and generic Webhook fields,
with all secret fields masked and excluded from diagnostics/log output.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `make test && rg -n 'uci set|ip route|firewall' src/diagnostics.c`

Expected: Tests PASS; the `rg` command returns no matches.

- [ ] **Step 5: Commit**

```bash
git add src/diagnostics.h src/diagnostics.c tests/test_diagnostics.c src/config.h luci-app-doorfast/luasrc/model/cbi/doorfast.lua README.md
git commit -m "feat: add read-only Doorfast diagnostics"
```

## Follow-on roadmap gates

The following work is intentionally not included in this foundation plan:

1. **Verified active controls:** opens, hangups, elevator calls, and floor
   actions require the evidence gate below.
2. **Experimental host mode:** requires independent registration and call-flow
   fixtures plus a user-visible acknowledgement that an indoor station may be
   affected. It must be packaged as disabled by default.
3. **Video:** requires independent design and tests for authenticated access,
   storage limits, RTSP passthrough or controlled relay, and proof that the
   existing indoor-station video function continues to work.

## Evidence gate for active-control plan

The next plan begins only after the repository contains anonymized fixtures
captured from a user-owned Dnake device for each of these interactions:

1. an incoming call and the corresponding hangup;
2. an administrator-authorized door-open command and response;
3. an administrator-authorized elevator request and response.

The active-control plan will define exact parser and transport tests from those
fixtures. It will not derive wire commands from the protected old program or
from unauthenticated external traffic.
