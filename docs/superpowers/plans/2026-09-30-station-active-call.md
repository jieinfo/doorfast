# 门口机独立主动呼叫与可视对讲实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为每台门口机增加独立的主动呼叫、挂断、视频复用和双向语音能力。

**架构：** Doorfast 核心在现有按 station 的媒体会话管理器上增加通话升级状态机，严格复用抓包确认的 `03/04 → 03/84 → 03/03 → 03/83` 流程，并由一个全局通话锁保证同时只有一个正式通话。HA 集成按 station 暴露 Call/Hang up 按钮和状态，复用现有 WebRTC 视频、PCM 上行与音频快照下行接口。

**技术栈：** C、现有 GVS UDP/ubus/HTTP runtime、Python Home Assistant custom component、Node.js 前端测试、Unity-style C test runner。

**规格：** `docs/superpowers/specs/2026-09-30-station-active-call-design.md`

## 全局约束

- 只允许一个 station 进入正式通话；其他 station 可以预览。
- 控制帧只能使用已确认的 `03/04`、`03/84`、`03/03`、`03/83`、`03/51`、`03/52`、`03/02`、`03/82`。
- 所有控制确认必须匹配 station identity 和 media generation。
- 不修改持久化 station 配置，不在实机上发送未经测试的协议帧。
- 保留旧的无 station 参数 `answer`/`hangup` 兼容入口。

---

### 任务 1：扩展 GVS 通话命令与 station 绑定

**文件：**
- 修改：`src/gvs_call_command.{h,c}`、`src/gvs_call_ack.{h,c}`、`src/gvs_call_control.{h,c}`、`src/gvs_call_runtime.{h,c}`
- 修改：`src/gvs_udp_sender.{h,c}`（按 station 发送和路由）
- 测试：`tests/test_gvs_call_command.c`、`tests/test_gvs_call_ack.c`、`tests/test_gvs_call_control.c`、`tests/test_gvs_call_runtime.c`、`tests/test_gvs_udp_sender.c`

- [ ] **步骤 1：编写失败测试**：覆盖指定 station 的 `03/03` payload、错误 station/generation 的 `03/83` 被忽略、通话与挂断确认状态转换。
- [ ] **步骤 2：运行定向测试确认失败**：`make test` 或项目现有测试构建命令运行上述测试，记录缺少 station call promotion 的失败。
- [ ] **步骤 3：实现最小接口**：增加 station identity/generation 参数，复用已有 `prepare_answer`/`prepare_hangup` 编码器，新增 `prepare_call` 或等价升级入口，保持现有 answer 调用行为。
- [ ] **步骤 4：运行定向测试确认通过**：验证帧字节、目标地址、确认匹配和超时行为。
- [ ] **步骤 5：提交**：`git add src tests && git commit -m "feat: bind gvs talk control to station"`

### 任务 2：实现 runtime 的按 station call/hangup API 和通话锁

**文件：**
- 修改：`src/media_session.{h,c}`、`src/media_session_manager.{h,c}`、`src/runtime_service.{h,c}`、`src/runtime_ubus.{h,c}`、`src/pcm_http_ubus.{h,c}`
- 修改：`src/event_stream.{h,c}` 或现有状态事件序列化文件
- 测试：`tests/test_media_session.c`、`tests/test_media_module.c`、`tests/test_runtime_ubus.c`、`tests/test_doorfast_http.sh`

- [ ] **步骤 1：编写失败测试**：覆盖 `call(station_id)` 从预览升级、全局通话锁、`busy`、`invalid_station`、`station_unavailable`、`ack_timeout`、按 station 挂断和状态事件。
- [ ] **步骤 2：运行定向测试确认失败**：运行现有 C 测试和 HTTP/ubus smoke tests，确认新 API 尚不存在。
- [ ] **步骤 3：实现最小状态机**：为 station 增加 `idle/preview/calling/talking/ending/failed`，调用已有 monitor 启动和 GVS call runtime；只在对应 `03/84`、`03/83`、`03/82` 到达时推进状态；添加结构化错误响应。
- [ ] **步骤 4：接入 HTTP/ubus**：实现 `POST /api/v1/call`、`POST /api/v1/hangup`、状态输出中的全局通话和 station 状态，同时保留旧 answer/hangup。
- [ ] **步骤 5：运行定向测试确认通过**：验证三 station 隔离、旧 API 兼容和错误状态清理。
- [ ] **步骤 6：提交**：`git add src tests && git commit -m "feat: add station call runtime API"`

### 任务 3：接通 8302 入站音频与通话媒体生命周期

**文件：**
- 修改：`src/gvs_audio_buffer.{h,c}`、`src/gvs_pcm_ingress.{h,c}`、`src/runtime_media_module.{h,c}`、`src/pcm_http.{h,c}`
- 修改：`src/media_session_manager.{h,c}`（通话开始/结束时启停音频和保活）
- 测试：`tests/test_gvs_audio_buffer.c`、`tests/test_gvs_pcm_ingress.c`、`tests/test_gvs_udp_sender.c`、`tests/test_media_module.c`、`tests/run_doorfast_vm_pcm_http.py`

- [ ] **步骤 1：编写失败测试**：验证 talking 状态产生 8302 入站 PCM 快照、发送本地 PCM 到匹配 station、挂断清除音频和保活任务、generation 切换不泄漏旧音频。
- [ ] **步骤 2：运行定向测试确认失败**：运行音频和 PCM HTTP 测试，记录缺少 station 绑定或生命周期断言。
- [ ] **步骤 3：实现最小接线**：将现有 G.711 A-law 解码/缓存和音频发送器绑定到当前通话 station；沿用现有 generation、revision、token 校验；挂断、超时和 media failure 统一释放资源。
- [ ] **步骤 4：运行定向测试确认通过**：确认音频路由、缓存快照、PCM 上行和清理行为。
- [ ] **步骤 5：提交**：`git add src tests && git commit -m "feat: attach station audio to active calls"`

### 任务 4：HA client、coordinator 和按 station 实体

**文件：**
- 修改：`custom_components/doorfast/client.py`、`custom_components/doorfast/client_types.py`、`custom_components/doorfast/monitor.py`
- 修改：`custom_components/doorfast/button.py`、`custom_components/doorfast/station_entity.py`、`custom_components/doorfast/__init__.py`
- 修改：`custom_components/doorfast/services.yaml`
- 测试：`tests/test_client.py`、`tests/test_monitor.py`、`tests/test_setup_lifecycle.py`、新增 `tests/test_station_call_entities.py`

- [ ] **步骤 1：编写失败测试**：验证 client 发送 station_id、错误映射、coordinator 保存 calling/talking/failed、每个 station 生成独立 Call/Hang up button。
- [ ] **步骤 2：运行定向测试确认失败**：`pytest tests/test_client.py tests/test_monitor.py tests/test_station_call_entities.py -q`。
- [ ] **步骤 3：实现 client/API**：增加 `call_station`、`hangup_station` 和状态解析；将服务异常转换为稳定的 HA 异常和可显示错误。
- [ ] **步骤 4：实现实体注册**：按 station 唯一 ID 注册按钮，按钮只调用目标 station，现有全局按钮继续兼容。
- [ ] **步骤 5：运行定向测试确认通过**：验证三个 station 的请求互不串台、实体卸载和状态刷新。
- [ ] **步骤 6：提交**：`git add custom_components tests && git commit -m "feat: add per-station call controls to ha"`

### 任务 5：HA 通话音频播放、PTT 绑定和翻译

**文件：**
- 修改：`custom_components/doorfast/pcm.py`、`custom_components/doorfast/websocket.py`、`custom_components/doorfast/frontend/audio-stream.mjs`、`custom_components/doorfast/frontend/doorfast-ptt-card.mjs`
- 修改：`custom_components/doorfast/translations/en.json`、`custom_components/doorfast/translations/zh-Hans.json`
- 测试：`tests/test_pcm.py`、`tests/test_pcm_websocket.py`、`tests/js/test_audio_stream.mjs`、`tests/js/test_ptt_card.mjs`

- [ ] **步骤 1：编写失败测试**：验证 talking station 才能播放 8302 音频、切换 generation 停止旧流、挂断关闭播放和 PTT、按钮及状态有中英文翻译。
- [ ] **步骤 2：运行定向测试确认失败**：运行 Python 与 Node 测试，记录当前仅有预览或全局音频的缺口。
- [ ] **步骤 3：实现最小前端接线**：将最新音频快照或 websocket PCM 流绑定到 station/generation；确保浏览器关闭或挂断会释放任务；复用已有麦克风上行批处理。
- [ ] **步骤 4：运行定向测试确认通过**：`pytest tests/test_pcm.py tests/test_pcm_websocket.py -q` 与 `node --test tests/js/test_audio_stream.mjs tests/js/test_ptt_card.mjs`。
- [ ] **步骤 5：提交**：`git add custom_components tests && git commit -m "feat: play station call audio in ha"`

### 任务 6：端到端回归、协议模拟与实机验收准备

**文件：**
- 修改：`tests/run_gvs_vm_ubus_call.py`、`tests/run_doorfast_vm_pcm_http.py` 或现有端到端 runner
- 创建：`tests/run_station_call_acceptance.py`
- 修改：`docs/superpowers/specs/2026-09-30-station-active-call-design.md`（记录已验证的限制或结果）

- [ ] **步骤 1：编写协议模拟测试**：模拟三台 station，逐台验证 `03/04 → 03/84 → 03/03 → 03/83 → 保活 → 03/02 → 03/82`，并验证占线和超时。
- [ ] **步骤 2：运行模拟测试确认失败**：在完整实现前确认 runner 能捕获错误 station、旧 generation 和残留音频。
- [ ] **步骤 3：补齐 runner 与清理断言**：让测试检查 UDP 8300/8302/8303、HTTP 状态、无残留任务和错误码。
- [ ] **步骤 4：运行全量验证**：C 测试、HA Python/Node 测试、shell smoke tests 和端到端模拟。
- [ ] **步骤 5：提交**：`git add tests docs && git commit -m "test: cover station active call acceptance"`
- [ ] **步骤 6：实机前检查**：只安装构建产物，不改持久化配置；逐台呼叫、保持至少 10 秒、挂断并抓包，记录结果后再决定是否创建 PR。
