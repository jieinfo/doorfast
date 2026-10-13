# Doorfast 会话可观测性实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在不改变控制和媒体时序的前提下，为 Doorfast status/事件增加会话结束原因、计时器剩余时间和稳定故障分类。

**架构：** 状态机在终止边界记录一次 terminal snapshot；状态接口把活动 deadline 转换为相对 `remaining_ms`。媒体 session 将现有数值错误映射为稳定字符串，同时保留旧字段。

**技术栈：** C11、ubus JSON、现有 Doorfast C 单元测试与 shell CGI fixture。

**规格：** `docs/superpowers/specs/2026-10-13-session-observability-design.md`

## 全局约束

- 不修改 GVS 控制码、已有 timeout 数值、预览重试、音频、媒体 ABI v3 或门禁时序。
- `remaining_ms` 只能是 Doorfast 计算的相对值，不输出 monotonic deadline。
- 新字段缺失或无法分类时使用 `unknown`；不从 HA 或前端推断原因。
- 保留所有已有状态字段和旧数值错误码。
- 日志和状态不得包含令牌、密码、原始媒体正文、SDP 或门禁材料。

## 文件结构

- 修改：`src/gvs_deadline.h`, `src/gvs_deadline.c` —— 提供剩余时间快照。
- 修改：`src/gvs_call_runtime.h`, `src/gvs_call_runtime.c`, `src/gvs_call_control.h`, `src/gvs_call_control.c` —— 记录确认/会话终止结果。
- 修改：`src/runtime_ubus.h`, `src/runtime_ubus.c`, `src/runtime_service.c` —— 保存 terminal snapshot 并输出 call/media 字段。
- 修改：`src/media_session.h`, `src/media_session.c`, `src/media_session_manager.h`, `src/media_session_manager.c` —— 映射媒体失败原因和剩余计时器。
- 修改：`package/doorfast/files/doorfast-http.sh` —— 保持 CGI 状态字段透传并过滤旧 bridge 缺失值。
- 测试：`tests/test_gvs_deadline.c`, `tests/test_gvs_call_control.c`, `tests/test_runtime_ubus.c`, `tests/test_media_session.c`, `tests/test_media_session_manager.c`, `tests/test_doorfast_http.sh`。

### 任务 1：为 deadline 提供相对剩余时间

**文件：** `src/gvs_deadline.h`, `src/gvs_deadline.c`, `tests/test_gvs_deadline.c`

- [ ] **步骤 1：编写失败测试**：覆盖未 armed 返回 `timer=none/remaining=0`、活动 deadline 返回不超过原 timeout 的剩余值、now 超过 deadline 返回 0、时间倒退返回错误。
- [ ] **步骤 2：运行测试验证失败**：`make test`；预期新增快照接口未定义而失败。
- [ ] **步骤 3：实现最小接口**：增加只读 `df_gvs_deadline_remaining()`，不改变 `tick()` 的状态转移；由调用者提供当前 `now_ms` 和 timer 名称。
- [ ] **步骤 4：运行测试验证通过**：`make test`；预期完整 C 测试通过。
- [ ] **步骤 5：Commit**：`git add src/gvs_deadline.* tests/test_gvs_deadline.c && git commit -m "feat: expose relative deadline remaining time"`

### 任务 2：记录 call terminal snapshot 和 fault 分类

**文件：** `src/gvs_call_runtime.*`, `src/gvs_call_control.*`, `src/runtime_ubus.h`, `src/runtime_ubus.c`, `src/runtime_service.c`, `tests/test_gvs_call_control.c`, `tests/test_runtime_ubus.c`

- [ ] **步骤 1：编写失败测试**：分别驱动 local hangup、peer hangup、session timeout、confirmation timeout、preemption、dispatch failure、runtime reset；断言 `end_reason`、`fault`、`last_station_id`、`last_generation` 和 timer 清零。
- [ ] **步骤 2：运行测试验证失败**：`make test`；预期 terminal 字段断言失败。
- [ ] **步骤 3：实现最小记录**：在真正终止的状态转移处写入一次 terminal snapshot；确认超时只写 `confirmation_timeout`，session deadline 才写 `session_timeout`；迟到帧只能被拒绝，不能改写新 generation。
- [ ] **步骤 4：扩展 ubus status**：在 `call` 中输出稳定枚举、最后 station/generation、当前 timer 和 relative remaining；活动会话不把上一通话原因显示为当前故障。
- [ ] **步骤 5：运行测试验证通过**：`make test`；该目标会构建并运行完整 `build/doorfast-tests`。
- [ ] **步骤 6：Commit**：`git add src/gvs_call_runtime.* src/gvs_call_control.* src/runtime_ubus.* src/runtime_service.c tests/test_gvs_call_control.c tests/test_runtime_ubus.c && git commit -m "feat: classify call termination and faults"`

### 任务 3：补齐 media session 终止原因与剩余时间

**文件：** `src/media_session.*`, `src/media_session_manager.*`, `src/runtime_ubus.c`, `tests/test_media_session.c`, `tests/test_media_session_manager.c`

- [ ] **步骤 1：编写失败测试**：覆盖 capacity busy、first-frame timeout、source stall、encoder/publisher failure、explicit stop、call preemption、daemon reset；断言 `failure_reason`/`end_reason`、timer 和 remaining。
- [ ] **步骤 2：运行测试验证失败**：`make test`；预期新枚举和字段断言失败。
- [ ] **步骤 3：实现最小映射**：将已有 `last_error` 和状态边界映射到稳定字符串；不改变 ABI 数组布局和 producer 生命周期。
- [ ] **步骤 4：输出状态**：在每个 `media.sessions[]` 项输出 timer、remaining、end_reason、failure_reason；活动发布只显示当前原因，结束后只保留最近终止快照。
- [ ] **步骤 5：运行测试验证通过**：`make test`；该目标会构建并运行完整 `build/doorfast-tests`。
- [ ] **步骤 6：Commit**：`git add src/media_session.* src/media_session_manager.* src/runtime_ubus.c tests/test_media_session.c tests/test_media_session_manager.c && git commit -m "feat: expose media termination diagnostics"`

### 任务 4：CGI、manifest 和主程序回归

**文件：** `package/doorfast/files/doorfast-http.sh`, `tests/test_doorfast_http.sh`, `tests/test_package_manifest.sh`

- [ ] **步骤 1：编写失败测试**：status fixture 包含新增 call/media 字段；测试旧 fixture 缺字段仍返回兼容 JSON，敏感字段仍被过滤。
- [ ] **步骤 2：运行测试验证失败**：`sh tests/test_doorfast_http.sh`；预期新增字段断言失败。
- [ ] **步骤 3：实现最小透传**：CGI 继续使用 ubus status，新增字段不重新解释；对旧 bridge 保持字段缺失兼容。
- [ ] **步骤 4：运行完整主程序验证**：`make test && build/doorfast-tests && make doorfast && sh tests/test_package_manifest.sh && git diff --check`。
- [ ] **步骤 5：Commit**：`git add package/doorfast/files/doorfast-http.sh tests/test_doorfast_http.sh tests/test_package_manifest.sh && git commit -m "test: cover session observability bridge compatibility"`

## 完成门槛

- 四个任务的 focused tests 和主程序完整测试通过。
- `git diff --check`、包清单、LuCI/CGI status fixture 通过。
- 实机 status 对 1、2、B2 各观察一次正常停止、一次会话结束或媒体失败；不执行未经授权的物理呼叫或开门。
