# Doorfast 主动呼叫会话归属修复实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让指定门口机的主动呼叫严格经过预览确认、真实主会话升级和有界失败清理，不再出现媒体显示 `call` 而主控制会话仍为 `idle`。

**架构：** 在 Doorfast 运行时增加单一 station-scoped 主动呼叫操作记录，区分 operation、控制 generation 和媒体 generation。媒体模块只负责预览/视频资源及原地升级，主控制状态机负责 `03/03`、`03/83`、保活、挂断和真实 `TALKING`；两者通过受控的 operation 状态和精确 key 交接。

**技术栈：** C17、现有 GVS 控制/ACK/monitor 状态机、媒体模块 ABI v3、ubus/CGI JSON、Makefile host tests。

**规格：** `docs/superpowers/specs/2026-10-14-outbound-call-ownership-repair-design.md` 与 `docs/superpowers/specs/2026-09-30-station-active-call-design.md`

## 全局约束

- 不改变来电自动化、门禁时序、音频算法、普通预览重试/冷却、媒体 ABI v3 结构布局和函数表。
- 不引入抓包中没有证据的新 GVS 控制帧；只使用 `03/04`、`03/84`、`03/03`、`03/83`、`03/51`/`03/52`、`03/02`/`03/82`。
- HTTP/ubus 接受请求只表示排队；现场完成状态从权威 status 读取。
- 控制 generation 与媒体 generation 永不直接等值比较；所有 station/runtime/key 需要精确匹配。
- 每一项生产改动先添加能抓住旧行为的失败测试，确认失败原因后再写最小实现。
- 测试、提交和本地审查在推送/PR/实机安装前完成；不轮询 Actions。

## 文件职责

- 修改：`src/runtime_service.c`、`src/runtime_service.h`：保存运行时主动呼叫 owner，驱动准备/升级/终态交接。
- 修改：`src/runtime_ubus.c`、`src/runtime_ubus.h`、`package/doorfast/files/doorfast-http.sh`：按 station 的 call/hangup 请求、状态字段和 operation_id 校验。
- 修改：`src/runtime_media_module.c`、`src/runtime_media_module.h`：增加受控的出站 preview→call 入口，保持 ABI v3 公开布局不变。
- 修改：`src/media_session_manager.c`、`src/media_session_manager.h`、`src/media_session.c`：原地升级和精确媒体清理。
- 修改：`src/gvs_call_control.c`、`src/gvs_call_runtime.c`：只在真实 owner 上提交/推进，不放宽 current guard。
- 测试：`tests/test_runtime_service.c`、`tests/test_runtime_ubus.c`、`tests/test_runtime_media_module.c`、`tests/test_media_session_manager.c`、`tests/test_gvs_monitor.c`、`tests/test_gvs_call_control.c`。

---

### 任务 1：锁定真实运行时断点和主动呼叫状态模型

**文件：**
- 测试：`tests/test_runtime_service.c`、`tests/test_runtime_ubus.c`
- 修改：`src/runtime_service.h`、`src/runtime_service.c`、`src/runtime_ubus.h`

- [ ] **步骤 1：编写失败测试**

  在真实 `df_runtime_ubus_call_station`→绑定 submit callback 路径上构造一个 station call，断言提交后 callback 收到的 session owner 是实际运行时 `session`，而不是只在栈上复制的 PREVIEW；断言主 session generation、peer 和状态保持可供 dispatch current 检查。另加测试：媒体 generation 与 control generation 不同时，station hangup 不能把媒体 generation 当作主控制 generation。

- [ ] **步骤 2：运行测试确认是预期失败**

  运行：`make -B build/doorfast-tests && build/doorfast-tests --runtime-service --runtime-ubus`

  预期：新测试失败在主 session 仍为 `idle` 或 generation 不匹配，不能以编译错误作为红灯。

- [ ] **步骤 3：实现最小 owner 状态**

  在运行时 owner 中增加一个有限的 station-scoped operation 记录：operation_id、station_id、media_generation、control_generation、stage、owns_preview、错误/计时字段。call submit 只安装该记录和真实控制 owner 的准备上下文；不在 HTTP handler 内等待 ACK，不创建第二个控制会话。

- [ ] **步骤 4：运行测试确认通过**

  运行同一命令；预期新增 owner/cross-generation 测试通过，已有 runtime/ubus 测试保持通过。

- [ ] **步骤 5：Commit**

  `git add src/runtime_service.c src/runtime_service.h src/runtime_ubus.c src/runtime_ubus.h tests/test_runtime_service.c tests/test_runtime_ubus.c && git commit -m "fix: bind outbound calls to runtime owner"`

### 任务 2：补齐预览确认后再升级的媒体入口

**文件：**
- 测试：`tests/test_runtime_media_module.c`、`tests/test_media_session_manager.c`、`tests/test_gvs_monitor.c`
- 修改：`src/runtime_media_module.c`、`src/runtime_media_module.h`、`src/media_session_manager.c`、`src/media_session_manager.h`、`src/media_session.c`

- [ ] **步骤 1：编写失败测试**

  通过真实 media session manager fixture 验证：主动 call 在没有当前 monitor `03/84` 确认时只保留预览/准备阶段，不进入 `DF_MEDIA_SESSION_CALL`；收到匹配确认后原地升级，public media generation、viewer 和 encoder ownership 保持；另一 station 的 active CALL 或同 station stale generation 返回明确 busy/generation mismatch。

- [ ] **步骤 2：运行测试确认是预期失败**

  运行：`make -B build/doorfast-tests && build/doorfast-tests --runtime-media-module --media-session-manager --gvs-monitor`

  预期：旧实现会直接创建/绑定 CALL，新增断言失败；确认不是 fixture 构造或 API 拼写错误。

- [ ] **步骤 3：实现最小出站升级入口**

  保持 v3 API 表布局不变；在现有 `start(..., CALL, request_generation=0)` 分支内让运行时使用精确 station 的已确认预览做原地升级。非零 request_generation 的 incoming call 分支保持现有行为。升级前执行 capacity/route/preflight，升级失败回滚候选资源；不发送第二个 preview keepalive owner。

- [ ] **步骤 4：运行测试确认通过**

  运行同一命令，再运行 `make -B build/doorfast-tests`；预期确认门控、原地 upgrade、busy/stale key 和资源 rollback 测试全部通过。

- [ ] **步骤 5：Commit**

  `git add src/runtime_media_module.c src/runtime_media_module.h src/media_session_manager.c src/media_session_manager.h src/media_session.c tests/test_runtime_media_module.c tests/test_media_session_manager.c tests/test_gvs_monitor.c && git commit -m "fix: gate outbound media upgrade on preview confirmation"`

### 任务 3：接通真实 `03/03`/ACK/`TALKING` 生命周期

**文件：**
- 测试：`tests/test_runtime_service.c`、`tests/test_gvs_call_control.c`、`tests/test_gvs_call_ack.c`
- 修改：`src/runtime_service.c`、`src/gvs_call_control.c`、`src/gvs_call_runtime.c`

- [ ] **步骤 1：编写失败测试**

  使用真实 dispatch/ACK/receive 路径：准备确认后提交 `CALL` 必须发送正确目标和 payload；发送后只有匹配同 station/current control generation 的 `03/83` 才能转为 `TALKING`；旧 generation、错误地址、错误端口 payload、未确认预览均保持 calling/失败，不得启动正式 audio identity。

- [ ] **步骤 2：运行测试确认是预期失败**

  运行：`make -B build/doorfast-tests && build/doorfast-tests --runtime-service --gvs-call-control --gvs-call-ack`

  预期：旧临时 session 路径在 dispatch current guard 被取消，新行为断言失败。

- [ ] **步骤 3：实现最小协调推进**

  在主循环已有 call control step/receive 边界推进 operation：预览有效后安装真实 control owner，提交一次 `03/03`；复用现有 1 秒 ACK 计时和当前 guard。确认后记录 `control_generation`、启动正式 session deadline/handshake；所有错误转换为有限 operation terminal，不放宽 dispatch 或 ACK 校验。

- [ ] **步骤 4：运行测试确认通过**

  运行同一 focused 命令和 `make -B build/doorfast-tests`；预期发送/ACK/旧包隔离、TALKING/audio admission 测试通过。

- [ ] **步骤 5：Commit**

  `git add src/runtime_service.c src/gvs_call_control.c src/gvs_call_runtime.c tests/test_runtime_service.c tests/test_gvs_call_control.c tests/test_gvs_call_ack.c && git commit -m "fix: complete outbound call control lifecycle"`

### 任务 4：实现有界取消、挂断和资源清理

**文件：**
- 测试：`tests/test_runtime_ubus.c`、`tests/test_runtime_service.c`、`tests/test_media_session_manager.c`
- 修改：`src/runtime_ubus.c`、`src/runtime_ubus.h`、`src/runtime_service.c`、`src/media_session_manager.c`

- [ ] **步骤 1：编写失败测试**

  覆盖准备超时、dispatch/ACK timeout、peer hangup、显式 station hangup、重复 hangup、借用预览和自建预览。断言：借用预览不因准备取消被停止；自建资源最终释放；已发送正式挂断只由控制 owner 发 `03/02`；媒体 STOP 使用 media key；清理成功但控制提交失败时返回结构化控制错误而不是假成功。

- [ ] **步骤 2：运行测试确认是预期失败**

  运行：`make -B build/doorfast-tests && build/doorfast-tests --runtime-ubus --runtime-service --media-session-manager`

  预期：旧 hangup 直接使用媒体 generation，且准备失败没有操作级 rollback，新增断言失败。

- [ ] **步骤 3：实现最小终态清理**

  让 operation stage 和 owner key 统一驱动终态；按 owns_preview 区分停止；挂断只验证当前 station/runtime/operation（兼容旧无 operation 调用），控制提交和媒体 stop 分别保留错误，保证重复清理幂等但不吞真实失败。保留既有 cooldown。

- [ ] **步骤 4：运行测试确认通过**

  运行同一 focused 命令和 `make -B build/doorfast-tests`；预期无活动 media、无残留 handshake/audio owner、终态 reason/fault 正确。

- [ ] **步骤 5：Commit**

  `git add src/runtime_ubus.c src/runtime_ubus.h src/runtime_service.c src/media_session_manager.c tests/test_runtime_ubus.c tests/test_runtime_service.c tests/test_media_session_manager.c && git commit -m "fix: clean outbound call owners deterministically"`

### 任务 5：发布权威 station-call 状态与 CGI 契约

**文件：**
- 测试：`tests/test_runtime_ubus.c`、`tests/test_runtime_service.c`、`tests/test_deployment_config.c`
- 修改：`src/runtime_ubus.h`、`src/runtime_ubus.c`、`package/doorfast/files/doorfast-http.sh`

- [ ] **步骤 1：编写失败测试**

  断言 status 输出 station_call 的白名单字段、startup/confirmation/session timer、剩余时间、operation/control/media identity 和稳定错误码；断言 malformed/旧 operation/runtime/station 被拒绝，且既有 status/call/hangup 调用保持兼容。

- [ ] **步骤 2：运行测试确认是预期失败**

  运行：`make -B build/doorfast-tests && build/doorfast-tests --runtime-ubus --runtime-service`

  预期：当前 status 没有 station_call，CGI 会丢弃 operation_id/调用所需字段，新增测试失败。

- [ ] **步骤 3：实现最小契约**

  在 ubus response/status 增加经过类型和枚举白名单校验的 station_call 对象；CGI 仅转发已验证 JSON 字段，保持无敏感正文输出。call/hangup 仍返回 queued/generation，并补充 operation/state，不把 queued 改成 completed。

- [ ] **步骤 4：运行测试确认通过**

  运行 focused 命令、`make -B build/doorfast-tests`、CGI shell fixture 和 `git diff --check`。

- [ ] **步骤 5：Commit**

  `git add src/runtime_ubus.c src/runtime_ubus.h package/doorfast/files/doorfast-http.sh tests/test_runtime_ubus.c tests/test_runtime_service.c tests/test_deployment_config.c && git commit -m "feat: expose station call operation status"`

### 任务 6：Doorfast 完整验证与本地交付检查

**文件：**
- 测试/构建：`Makefile` 既有 targets、`package/doorfast/Makefile`、LuCI 测试目录
- 文档记录：忽略目录 `.superpowers/sdd/2026-10-14-outbound-call-ownership-repair/`

- [ ] **步骤 1：运行完整验证**

  运行：`make -B test`、`make -B doorfast`、既有 CGI/包清单测试和 LuCI 测试套件；记录退出码、测试总数、失败数及已知编译警告。

- [ ] **步骤 2：执行变异检查**

  临时验证如下变异至少被测试捕获：把 call submit 的真实 owner 换回栈副本；绕过 `03/84` 门控；把媒体 generation 传给 hangup；将 purpose=call 作为 talking；清理后吞掉控制错误。恢复源码后重跑 focused tests。

- [ ] **步骤 3：检查最终 diff 和工作树**

  运行：`git diff --check`、`git diff --stat origin/codex/preview-startup-state...HEAD`、`git status --short --branch`，确认只包含计划内主程序、测试、CGI 和计划记录；不触碰 `.DS_Store` 或已有恢复目录。

- [ ] **步骤 4：Commit**

  `git add docs/superpowers/plans/2026-10-14-outbound-call-ownership-repair.md && git commit -m "docs: plan outbound call ownership repair"`

