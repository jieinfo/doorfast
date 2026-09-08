# Doorfast ubus 与 LuCI 状态页实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 ImmortalWrt 25.12.1 x86_64 上提供只读的 `doorfast.status` ubus 方法和独立 LuCI 状态页，同时保持 Doorfast 的 GVS 网络行为为纯被动模式。

**Architecture:** 现有同步状态快照继续作为唯一事实来源；新增 ubus 适配层把结构化快照转换为类型稳定的 blobmsg 响应，并由现有抓包循环以非阻塞方式处理。LuCI 作为独立架构无关 APK，只获得 `doorfast.status` 的只读 ACL，每 5 秒刷新一次。

**Tech Stack:** C17、libpcap、libubus、libubox/blobmsg、ImmortalWrt package.mk、LuCI JavaScript RPC、POSIX shell、GitHub Actions。

**Spec:** `docs/superpowers/specs/2026-09-08-doorfast-ubus-luci-status-design.md`

## Global Constraints

- 目标系统固定为 ImmortalWrt 25.12.1 x86_64；软件包格式为 APK。
- ubus 对象固定为 `doorfast`，唯一方法为无参数只读方法 `status`。
- LuCI 本阶段只显示状态，不修改 UCI、不调用 init 脚本、不提供门禁控制和升级入口。
- 不新增任何 GVS 网络发送；现有 `passive_only '1'` 默认值保持不变。
- 状态响应不得包含逻辑地址、IP、接口名、适配器键名和值、认证字段或原始报文。
- ubusd 失败不得终止门禁核心；重连间隔固定为 5000 毫秒。
- 正常抓包等待期间 ubus 最大调度延迟为约 1000 毫秒；捕获重连等待按不超过 250 毫秒的片段处理。
- 本机默认构建不依赖 libubus；只有目标包构建定义 `DF_WITH_UBUS`。
- 不覆盖或丢弃工作树中既有未提交修改；每次提交只暂存任务列出的文件或经过审阅的精确补丁块。

---

### Task 0: 固化现有同步运行时基线

**Files:**
- Review: `src/gvs_presence.c`, `src/gvs_sync.c`, `src/gvs_sync_adapters.c`, `src/gvs_runtime_sync.c`, `src/gvs_sync_state.c`
- Review: `src/runtime_service.c`, `src/config.c`, `src/runtime_config.c`, `src/gvs_identity.c`
- Review: matching headers, tests, UCI files, package recipe and sync documentation currently shown by `git status`

**Interfaces:**
- Consumes: the already implemented offline GVS presence, `91/03` sync, first-party adapter registry and redacted runtime snapshot.
- Produces: a clean, separately reviewed prerequisite commit on which ubus work can be based.

- [ ] **Step 1: Inventory the existing dirty baseline**

Run:

```sh
git status --short
git diff --check
git diff -- Makefile src tests package docs
```

Expected: only the previously developed identity, presence, sync, runtime, package and documentation changes are present; no ubus or LuCI implementation files exist yet.

- [ ] **Step 2: Verify the prerequisite behavior**

Run:

```sh
make -B test doorfast
sh tests/test_main_cli.sh
sh tests/test_package_manifest.sh
python3 -B -m unittest discover -s tests -p 'test_gvs_preemption_model.py'
```

Expected: all commands exit 0; the C suite includes the first-party adapter and redacted status snapshot tests.

- [ ] **Step 3: Commit only the reviewed prerequisite files**

Stage the exact files reported in Step 1 that belong to the existing GVS identity/sync/runtime work, inspect `git diff --cached`, then run:

```sh
git commit -m "feat: add offline GVS sync runtime"
```

Expected: the design commit remains separate, and `git status --short` is empty before starting Task 1.

---

### Task 1: Expose validated phase and role names

**Files:**
- Modify: `src/gvs_runtime_sync.h`
- Modify: `src/gvs_runtime_sync.c`
- Modify: `tests/test_gvs_runtime_sync.c`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `enum df_gvs_presence_phase`, `enum df_gvs_sync_role`.
- Produces: `const char *df_gvs_runtime_sync_phase_name(enum df_gvs_presence_phase phase)`.
- Produces: `const char *df_gvs_runtime_sync_role_name(enum df_gvs_sync_role role)`.
- Contract: valid values return the exact API strings; invalid values return `NULL`.

- [ ] **Step 1: Write failing behavior tests**

Add a test that uses literal expectations rather than the JSON serializer:

```c
void test_gvs_runtime_sync_names_only_valid_public_states(void) {
    TEST_ASSERT_INT_EQ(0, strcmp("down",
        df_gvs_runtime_sync_phase_name(DF_GVS_PRESENCE_DOWN)));
    TEST_ASSERT_INT_EQ(0, strcmp("wait_sync",
        df_gvs_runtime_sync_phase_name(DF_GVS_PRESENCE_WAIT_SYNC)));
    TEST_ASSERT_INT_EQ(0, strcmp("sync_ask",
        df_gvs_runtime_sync_phase_name(DF_GVS_PRESENCE_SYNC_ASK)));
    TEST_ASSERT_INT_EQ(0, strcmp("sync_choose",
        df_gvs_runtime_sync_phase_name(DF_GVS_PRESENCE_SYNC_CHOOSE)));
    TEST_ASSERT_INT_EQ(0, strcmp("periodic",
        df_gvs_runtime_sync_phase_name(DF_GVS_PRESENCE_PERIODIC)));
    TEST_ASSERT_INT_EQ(0, strcmp("maintainer",
        df_gvs_runtime_sync_role_name(DF_GVS_SYNC_ROLE_MAINTAINER)));
    TEST_ASSERT_INT_EQ(0, strcmp("follower",
        df_gvs_runtime_sync_role_name(DF_GVS_SYNC_ROLE_FOLLOWER)));
    TEST_ASSERT_INT_EQ(1, df_gvs_runtime_sync_phase_name(
        (enum df_gvs_presence_phase)99) == NULL);
    TEST_ASSERT_INT_EQ(1, df_gvs_runtime_sync_role_name(
        (enum df_gvs_sync_role)99) == NULL);
}
```

Declare and call the test from `tests/test_main.c`, and increase the suite count by one.

- [ ] **Step 2: Run the test to verify red state**

Run:

```sh
make test
```

Expected: compilation fails because the two public name functions are not declared.

- [ ] **Step 3: Publish the existing mappings**

Move the two private mapping functions out of static scope, declare them in the header, and return `NULL` for invalid values. Update `df_gvs_runtime_sync_status_json()` to reject a null phase or role before calling `snprintf`.

The exact declarations are:

```c
const char *df_gvs_runtime_sync_phase_name(
    enum df_gvs_presence_phase phase);
const char *df_gvs_runtime_sync_role_name(enum df_gvs_sync_role role);
```

- [ ] **Step 4: Verify green state and regression coverage**

Run:

```sh
make -B test doorfast
```

Expected: both binaries build with `-Werror`; all C tests exit 0.

- [ ] **Step 5: Commit the mapping contract**

```sh
git add src/gvs_runtime_sync.h src/gvs_runtime_sync.c tests/test_gvs_runtime_sync.c tests/test_main.c
git commit -m "refactor: expose validated GVS status names"
```

---

### Task 2: Add the host-safe ubus lifecycle adapter

**Files:**
- Create: `src/runtime_ubus.h`
- Create: `src/runtime_ubus.c`
- Create: `tests/test_runtime_ubus.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `struct df_gvs_runtime_sync_status` and the public phase/role name functions from Task 1.
- Produces: `typedef int (*df_runtime_status_provider_fn)(struct df_gvs_runtime_sync_status *status, void *context)`.
- Produces: the three lifecycle functions defined in the design specification.
- Contract: without `DF_WITH_UBUS`, lifecycle validation and monotonic time checks work but no socket, file or process is created.

- [ ] **Step 1: Write failing lifecycle tests**

Create a provider that returns a literal snapshot and counts calls. Test invalid arguments, nondecreasing time and harmless stop:

```c
static int provide_runtime_status(
    struct df_gvs_runtime_sync_status *status, void *context) {
    unsigned *calls = context;
    memset(status, 0, sizeof(*status));
    status->phase = DF_GVS_PRESENCE_PERIODIC;
    status->role = DF_GVS_SYNC_ROLE_FOLLOWER;
    status->sync_version = 23;
    (*calls)++;
    return DF_OK;
}

void test_runtime_ubus_stub_validates_lifecycle_without_side_effects(void) {
    struct df_runtime_ubus service = {0};
    unsigned calls = 0;

    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_start(NULL, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_start(&service, NULL, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_ubus_start(&service, provide_runtime_status, &calls, 10));
    TEST_ASSERT_INT_EQ(DF_OK, df_runtime_ubus_process(&service, 10));
    TEST_ASSERT_INT_EQ(DF_ERR_INVALID,
        df_runtime_ubus_process(&service, 9));
    TEST_ASSERT_INT_EQ(0, (int)calls);
    df_runtime_ubus_stop(&service);
    df_runtime_ubus_stop(&service);
}
```

Register the test in `tests/test_main.c` and increase the suite count.

- [ ] **Step 2: Run the test to verify red state**

Run:

```sh
make test
```

Expected: compilation fails because `runtime_ubus.h` and its functions do not exist.

- [ ] **Step 3: Implement the public host-safe shell**

Define the public state without exposing libubus types:

```c
struct df_runtime_ubus {
    df_runtime_status_provider_fn provide_status;
    void *status_context;
    void *platform;
    uint64_t last_now_ms;
    uint64_t next_reconnect_ms;
    bool started;
};
```

`df_runtime_ubus_start()` zeroes the structure, stores the provider and time, and marks it started. `df_runtime_ubus_process()` rejects null, stopped or backwards-time input. `df_runtime_ubus_stop()` releases the target implementation when present, then zeroes the structure. Wrap all libubus includes and target-only functions in `#ifdef DF_WITH_UBUS`; the default branch has no external dependency.

Add `src/runtime_ubus.c` and `tests/test_runtime_ubus.c` to the corresponding Makefile source lists.

- [ ] **Step 4: Implement the target libubus object**

Inside `DF_WITH_UBUS`, define a private implementation containing an embedded `struct ubus_context`, `struct ubus_object`, object type, method table, blob buffer, connection flag and pointer back to `df_runtime_ubus`.

Register exactly this method table:

```c
static const struct ubus_method df_runtime_ubus_methods[] = {
    UBUS_METHOD_NOARG("status", df_runtime_ubus_status_handler),
};
```

The handler must:

1. call `provide_status` into a zeroed local snapshot;
2. reject provider failure or null phase/role names with `UBUS_STATUS_UNKNOWN_ERROR`;
3. initialize a blob buffer;
4. add `running=true` and `mode="passive"`;
5. open a `sync` table and add every field from the specification with `blobmsg_add_u32`, `blobmsg_add_u8` or `blobmsg_add_string` according to its declared type;
6. close the table and call `ubus_send_reply`;
7. return `UBUS_STATUS_OK` only when the reply is sent successfully.

Connection logic uses `ubus_connect_ctx`, `ubus_add_object`, `poll` with zero timeout, and `ubus_handle_event`. `POLLERR`, `POLLHUP`, context EOF/error, registration failure or event failure closes the context and sets `next_reconnect_ms = last_now_ms + 5000U`. A reconnect attempt occurs only after that deadline.

- [ ] **Step 5: Verify host build and sanitizers**

Run:

```sh
make -B test doorfast
make -B test CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -I/opt/homebrew/Cellar/libpcap/1.10.6/include -fsanitize=address,undefined -fno-omit-frame-pointer' PCAP_LIBS='-L/opt/homebrew/Cellar/libpcap/1.10.6/lib -lpcap -fsanitize=address,undefined'
make -B test doorfast
```

Expected: normal and sanitizer C tests exit 0; the final command restores non-sanitized binaries.

- [ ] **Step 6: Commit the adapter**

```sh
git add Makefile src/runtime_ubus.h src/runtime_ubus.c tests/test_runtime_ubus.c tests/test_main.c
git commit -m "feat: add Doorfast ubus status adapter"
```

---

### Task 3: Pump ubus from the passive runtime loop

**Files:**
- Modify: `src/runtime_service.h`
- Modify: `src/runtime_service.c`
- Create: `tests/test_runtime_service.c`
- Modify: `Makefile`
- Modify: `tests/test_main.c`

**Interfaces:**
- Consumes: `df_runtime_ubus_start`, `df_runtime_ubus_process`, `df_runtime_ubus_stop`.
- Produces: `int df_runtime_pump_delay(unsigned delay_ms, unsigned max_slice_ms, df_runtime_delay_slice_fn run_slice, void *context)`.
- Contract: the delay helper emits slices whose sum equals `delay_ms`, with no slice larger than `max_slice_ms`, and stops on the first callback failure.

- [ ] **Step 1: Write failing delay slicing tests**

Use a fake callback that records literal slice sizes without sleeping:

```c
struct delay_trace {
    unsigned values[20];
    size_t count;
    bool fail_second;
};

static int record_delay_slice(unsigned delay_ms, void *context) {
    struct delay_trace *trace = context;
    trace->values[trace->count++] = delay_ms;
    if (trace->fail_second && trace->count == 2U) {
        return DF_ERR_IO;
    }
    return DF_OK;
}

void test_runtime_delay_is_pumped_in_bounded_slices(void) {
    struct delay_trace trace = {0};
    TEST_ASSERT_INT_EQ(DF_OK,
        df_runtime_pump_delay(1000, 250, record_delay_slice, &trace));
    TEST_ASSERT_INT_EQ(4, (int)trace.count);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[0]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[1]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[2]);
    TEST_ASSERT_INT_EQ(250, (int)trace.values[3]);
}
```

Add cases for a 251 millisecond delay producing `250, 1`, zero delay rejection, zero maximum rejection, and propagation of the second callback's `DF_ERR_IO`.

- [ ] **Step 2: Run the test to verify red state**

Run:

```sh
make test
```

Expected: compilation fails because the delay helper and callback type are not declared.

- [ ] **Step 3: Implement the bounded delay helper**

Declare:

```c
typedef int (*df_runtime_delay_slice_fn)(unsigned delay_ms, void *context);

int df_runtime_pump_delay(unsigned delay_ms, unsigned max_slice_ms,
                          df_runtime_delay_slice_fn run_slice,
                          void *context);
```

Implement a subtraction loop that chooses `min(remaining, max_slice_ms)`, calls the callback and returns immediately on non-`DF_OK`. It must not sleep itself.

- [ ] **Step 4: Wire the actual runtime lifecycle**

In `df_runtime_service_run()`:

- start ubus after `df_gvs_runtime_sync_start` succeeds;
- provide status through a small callback that calls `df_gvs_runtime_sync_status` on the live `sync` structure;
- call `df_runtime_ubus_process` once after every capture return and timer update;
- replace the capture-retry `nanosleep` with `df_runtime_pump_delay(delay_ms, 250U, runtime_wait_and_pump, &context)`;
- make `runtime_wait_and_pump` sleep for its slice, then call `df_runtime_ubus_process` with current monotonic time;
- treat ubus process failures as management degradation, log one redacted transition and allow the adapter's 5-second reconnect policy to proceed;
- call `df_runtime_ubus_stop` at the `done` label before closing capture.

The live status provider must never copy the logical identity into the snapshot or ubus response.

- [ ] **Step 5: Verify runtime and CLI regressions**

Run:

```sh
make -B test doorfast
sh tests/test_main_cli.sh
```

Expected: all tests exit 0; `doorfast --help` still works without opening ubus or capture; default host build has no libubus link dependency.

- [ ] **Step 6: Commit runtime integration**

```sh
git add Makefile src/runtime_service.h src/runtime_service.c tests/test_runtime_service.c tests/test_main.c
git commit -m "feat: serve ubus status during passive capture"
```

---

### Task 4: Package the read-only LuCI status page

**Files:**
- Create: `package/luci-app-doorfast/Makefile`
- Create: `package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json`
- Create: `package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json`
- Create: `package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js`
- Create: `package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js`
- Create: `tests/test_luci_status.js`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: ubus object `doorfast`, method `status`, no arguments.
- Produces: architecture-independent APK `luci-app-doorfast` and LuCI menu route `admin/services/doorfast`.
- Contract: ACL grants only `doorfast.status`; polling interval is 5 seconds; no write or command RPC exists.

- [ ] **Step 1: Extend the package test before creating assets**

Add executable checks for:

```sh
test -f package/luci-app-doorfast/Makefile
test -f package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json
test -f package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json
test -f package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js
test -f package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js
grep -q 'PKGARCH:=all' package/luci-app-doorfast/Makefile
grep -q '+doorfast +luci-base +rpcd' package/luci-app-doorfast/Makefile
python3 -m json.tool package/luci-app-doorfast/root/usr/share/luci/menu.d/luci-app-doorfast.json >/dev/null
python3 -m json.tool package/luci-app-doorfast/root/usr/share/rpcd/acl.d/luci-app-doorfast.json >/dev/null
! grep -R -E -q 'service|set|delete|add|exec|command' package/luci-app-doorfast/root/usr/share/rpcd/acl.d
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js
```

The ACL negative check applies only to JSON keys and values; if the description contains a blocked word, use the exact description `Read Doorfast runtime status`.

- [ ] **Step 2: Run the package test to verify red state**

Run:

```sh
sh tests/test_package_manifest.sh
```

Expected: failure at the first missing `luci-app-doorfast` file.

- [ ] **Step 3: Create the manual package recipe and assets**

Use a package.mk recipe with:

```make
PKG_NAME:=luci-app-doorfast
PKG_VERSION:=0.1.0
PKG_RELEASE:=1
PKGARCH:=all

define Package/luci-app-doorfast
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI support for Doorfast
  DEPENDS:=+doorfast +luci-base +rpcd
endef
```

The install recipe copies the menu, ACL, JavaScript model and view to the exact paths listed above. No post-install script, UCI default or service command is included.

The menu action is a `view` with path `doorfast/status` and requires the `luci-app-doorfast` ACL. The ACL JSON must contain only:

```json
{
  "luci-app-doorfast": {
    "description": "Read Doorfast runtime status",
    "read": {
      "ubus": {
        "doorfast": ["status"]
      }
    }
  }
}
```

- [ ] **Step 4: Implement and test the view model**

Create `status_model.js` with a pure `formatStatus(payload)` function. The file assigns the model to `module.exports` when CommonJS is present and returns the same model for LuCI's resource loader. Create `tests/test_luci_status.js` as a Node test that requires this exact installed model file. The model must map a literal successful payload to three sections and expose the exact unavailable and stale labels. Keep LuCI DOM creation outside the formatter so the formatter can be tested without a browser.

The RPC declaration is fixed:

```javascript
var callStatus = rpc.declare({
    object: 'doorfast',
    method: 'status',
    expect: { '': {} }
});
```

The view imports the model with `'require doorfast.status_model as statusModel';`. The formatter returns display rows for service, online synchronization and last sync frame. It must label `maintainer` as `同步维护者`, `follower` as `同步跟随者`, and `starting` as `选举中`; it must not use `主机模式已完成`. The view calls `poll.add()` with a 5-second interval and marks retained values as `陈旧` after a failed refresh.

Run:

```sh
node tests/test_luci_status.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js
node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/status.js
sh tests/test_package_manifest.sh
```

Expected: the formatter assertions, JavaScript syntax check and package checks exit 0.

- [ ] **Step 5: Commit the LuCI package**

```sh
git add package/luci-app-doorfast tests/test_luci_status.js tests/test_package_manifest.sh
git commit -m "feat: add read-only Doorfast LuCI status page"
```

---

### Task 5: Enable target ubus linking and build both APKs

**Files:**
- Modify: `package/doorfast/Makefile`
- Modify: `scripts/prepare-sdk-package.sh`
- Modify: `.github/workflows/build-apk.yml`
- Modify: `tests/test_package_manifest.sh`

**Interfaces:**
- Consumes: target-only code behind `DF_WITH_UBUS`, both package directories.
- Produces: reproducible SDK builds for `doorfast-*.apk` and `luci-app-doorfast-*.apk`.

- [ ] **Step 1: Add failing build-recipe assertions**

Extend the package test with exact behavioral checks:

```sh
grep -q '+libubus +libubox +libblobmsg-json' package/doorfast/Makefile
grep -q -- '-DDF_WITH_UBUS' package/doorfast/Makefile
grep -q -- '-lubus -lubox -lblobmsg_json' package/doorfast/Makefile
grep -q 'package/luci-app-doorfast' scripts/prepare-sdk-package.sh
grep -q 'feeds install.*luci-base' .github/workflows/build-apk.yml
grep -q 'package/luci-app-doorfast/compile' .github/workflows/build-apk.yml
grep -q 'luci-app-doorfast-\*.apk' .github/workflows/build-apk.yml
```

- [ ] **Step 2: Run the package test to verify red state**

Run:

```sh
sh tests/test_package_manifest.sh
```

Expected: failure because the daemon recipe lacks ubus dependencies and the SDK script copies only one package.

- [ ] **Step 3: Update the daemon target recipe**

Append `+libubus +libubox +libblobmsg-json` to `Package/doorfast/DEPENDS`. Add `-DDF_WITH_UBUS` to target compilation and link the daemon with:

```make
-lpcap -lubus -lubox -lblobmsg_json
```

Do not add those libraries to the host Makefile link flags.

- [ ] **Step 4: Copy and build both packages in the SDK**

Change `prepare-sdk-package.sh` to validate that neither destination exists, then copy:

```sh
cp -R "$repo_dir/package/doorfast" "$sdk_dir/package/doorfast"
cp -R "$repo_dir/package/luci-app-doorfast" "$sdk_dir/package/luci-app-doorfast"
cp -R "$repo_dir/src" "$sdk_dir/package/doorfast/src"
```

In GitHub Actions, install `luci-base`, build both package targets, and upload only these paths:

```yaml
path: |
  **/bin/packages/x86_64/base/doorfast-*.apk
  **/bin/packages/x86_64/base/luci-app-doorfast-*.apk
```

- [ ] **Step 5: Verify local manifests and host isolation**

Run:

```sh
sh tests/test_package_manifest.sh
make -B test doorfast
otool -L build/doorfast | grep -qv 'libubus'
```

Expected: package checks and host build exit 0; the host binary has no libubus dependency.

- [ ] **Step 6: Run or dispatch the target SDK build**

In the verified ImmortalWrt 25.12.1 x86_64 SDK, run:

```sh
./scripts/feeds update -a
./scripts/feeds install libpcap libuci libjson-c libopenssl luci-base
make defconfig
make package/doorfast/compile V=s
make package/luci-app-doorfast/compile V=s
```

Expected: both APK files exist under the SDK `bin/packages/x86_64/base/` directory. Record their paths and SHA-256 values; do not claim installation success from build success alone.

- [ ] **Step 7: Commit build integration**

```sh
git add package/doorfast/Makefile scripts/prepare-sdk-package.sh .github/workflows/build-apk.yml tests/test_package_manifest.sh
git commit -m "build: package Doorfast ubus and LuCI status"
```

---

### Task 6: Verify the full offline deliverable and document the device gate

**Files:**
- Modify: `docs/doorfast-host-mode-roadmap.md`
- Modify: `docs/2026-09-08_reverse-gvs-sync-report.md`
- Create: `docs/doorfast-ubus-luci-status.md`

**Interfaces:**
- Consumes: all implementation, package and CI work from Tasks 1–5.
- Produces: reproducible operator instructions, exact verified scope and remaining target-system acceptance gate.

- [ ] **Step 1: Run the complete fresh verification suite**

Run:

```sh
make -B test doorfast
sh tests/test_main_cli.sh
sh tests/test_package_manifest.sh
node tests/test_luci_status.js
python3 -B -m unittest discover -s tests -p 'test_gvs_preemption_model.py'
git diff --check
```

Expected: every command exits 0 with no compiler warnings, test failures or whitespace errors.

- [ ] **Step 2: Run memory-safety verification and restore release binaries**

Run:

```sh
make -B test CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -I/opt/homebrew/Cellar/libpcap/1.10.6/include -fsanitize=address,undefined -fno-omit-frame-pointer' PCAP_LIBS='-L/opt/homebrew/Cellar/libpcap/1.10.6/lib -lpcap -fsanitize=address,undefined'
make -B test doorfast
```

Expected: sanitizer execution exits 0, followed by successful non-sanitized rebuild.

- [ ] **Step 3: Write exact operator documentation**

Document installation and the only status command:

```sh
apk add ./doorfast-0.1.0-r1.apk ./luci-app-doorfast-0.1.0-r1.apk
ubus call doorfast status '{}'
```

Document each response field, the 5-second LuCI refresh, the unavailable/stale behavior, and removal commands. State explicitly that this milestone proves an offline management surface, not GVS device acceptance or complete host mode.

Update the roadmap M5 entry to mark the read-only status surface complete while leaving configuration, controls and upgrade unchecked. Add a report Evidence item containing the verification command, source hashes and APK hashes when available.

- [ ] **Step 4: Perform target runtime acceptance when an instance exists**

On an owned ImmortalWrt 25.12.1 x86_64 instance:

```sh
apk add ./doorfast-0.1.0-r1.apk ./luci-app-doorfast-0.1.0-r1.apk
uci set doorfast.main.enabled='1'
uci set doorfast.main.gvs_interface='br-lan'
uci set doorfast.main.gvs_local_address='610101010102'
uci commit doorfast
/etc/init.d/doorfast restart
ubus call doorfast status '{}'
/etc/init.d/doorfast stop
ubus call doorfast status '{}'
```

Expected: the first ubus call returns the specified typed structure; after stop, ubus reports object not found. Open `/cgi-bin/luci/admin/services/doorfast`, verify 5-second refresh, then interrupt the configured capture interface and confirm the page remains reachable while role becomes `down`. Use only a test interface belonging to the user; do not alter production routing or send GVS frames.

- [ ] **Step 5: Commit documentation and recorded evidence**

```sh
git add docs/doorfast-host-mode-roadmap.md docs/2026-09-08_reverse-gvs-sync-report.md docs/doorfast-ubus-luci-status.md
git commit -m "docs: record Doorfast ubus and LuCI status verification"
```

- [ ] **Step 6: Review the final branch**

Run:

```sh
git status --short
git log --oneline --decorate -8
git diff origin/feature/transparent-foundation...HEAD --check
```

Expected: no accidental generated artifacts are tracked, each task has a focused commit, and any unavailable target-runtime checks are reported as pending rather than passed.
