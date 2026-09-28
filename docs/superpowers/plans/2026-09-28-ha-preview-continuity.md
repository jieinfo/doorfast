# HA 预览租约与 WebRTC 连续性实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 一次 HA 原生摄像头打开能等待 Doorfast 的真实首帧，并在已取得 WebRTC answer 后保留同一发布租约，直到用户关闭或发布真正终止。

**架构：** `MonitorCoordinator` 把“租约仍属于当前公开 generation”和“此刻 ready/有实时源”分开；已协商的 WebRTC 会话只因明确终止而释放。首次协商仅对 producer 尚未出现作取消感知的重试；认证、配置、运行时身份、station 和来电抢占错误立即报错。Doorfast 在短流时维持 FFmpeg producer 并发送重连 JPEG，HA 不重建已拿到 answer 的浏览器链路。

**技术栈：** Python 3、Home Assistant CameraWebRTCProvider、aiohttp、go2rtc WebSocket、`unittest`。

**规格：** Doorfast 工作树中的 `docs/superpowers/specs/2026-09-28-preview-continuity-design.md`；Doorfast 配套计划是 `docs/superpowers/plans/2026-09-28-doorfast-preview-continuity.md`。本计划的代码路径相对于 HA 检出 `/private/tmp/doorfastforha-webrtc-recovery`，当前基线为 `origin/main`／`v0.3.14` 的 `281c5bf`。执行前重新确认分支与工作树，不覆盖用户后续改动。

## 全局约束

- 不改 HA 原生前端、不引入自定义卡片、不增加音频、主动呼叫或持久配置项。
- 门口机忙的 `03/50` 不是实时视频成功；只有真实 JPEG／浏览器真实解码帧计入成功。60 秒是验收指标，不是让 HA 主动放弃仍打开的请求的超时。
- `requesting`、`stopping` 与新门口机尝试可恢复；认证、配置、station 不存在、运行时身份变化、来电抢占必须明确失败。
- 首次观看者等真实首帧与 producer；同一发布的后来观看者可立即看到 Doorfast 的重连提示。
- answer 后同一公开 generation 的租约仍有效且 producer 存活时，不因 `source_live=false` 清理；producer 真的结束或 generation 被抢占时发明确 WebRTC 错误并释放租约。
- 页面关闭、协商取消、配置卸载、并发清理各只释放自己拥有的 lease 一次；失败恢复不可越过明确关闭。
- HA HACS 更新由用户执行；Actions 通过前不实机安装。

## 文件职责

| 文件 | 职责 |
| --- | --- |
| `custom_components/doorfast/monitor.py` | 严格解析发布 `ready` 与 `encoder_running`，提供不依赖实时源状态的租约所有权判断，处理首帧前可恢复的 stopping。 |
| `custom_components/doorfast/webrtc.py` | 只重试可恢复的首次 producer 协商；已协商会话按租约与发布终止状态 reconcile，分类错误且保证清理一次。 |
| `custom_components/doorfast/const.py` | 取消集成自设的固定首帧截止时间，不增加配置项。 |
| `tests/test_monitor.py` | status 顺序、停止／恢复、租约所有权、并发与取消测试。 |
| `tests/test_webrtc.py` | offer 错误分类、answer 后暂时源变化／真正终止、并发观看、关闭与卸载测试。 |
| `tests/test_setup_lifecycle.py` | 注册表状态轮询与 provider reconcile 集成测试。 |

### 任务 1：租约所有权与发布可播放状态分离

**文件：** 修改 `custom_components/doorfast/monitor.py`、`tests/test_monitor.py`。

- [ ] **步骤 1：编写失败的测试。** 对同一 generation 已登记 lease，先送 `ready=true, encoder_running=true`，再送更高 revision 的 `state=requesting, ready=false, encoder_running=true`；断言 `lease_owned(lease)` 仍真但 `ready` 假。送 `state=failed` 或 `monitor_preempted` 后断言 ownership 假。单独断言显式 `ready=false` 不能被 `state=publishing` 的旧启发式覆盖；较旧 revision 不能使租约复活。

```python
self.assertTrue(coordinator.lease_owned(lease))
await coordinator.async_apply_status({"generation": lease.generation,
    "state": "requesting", "ready": False, "encoder_running": True,
    "status_revision": 8})
self.assertTrue(coordinator.lease_owned(lease))
self.assertFalse(coordinator.ready)
```

- [ ] **步骤 2：运行 `python3 -m unittest tests.test_monitor -v`，预期 `lease_owned` 不存在或 ready 断言失败。**
- [ ] **步骤 3：实现下面的私有状态与接口。** `_publisher_running: bool | None` 只在 payload 明确包含布尔 `encoder_running` 时更新；显式 `ready` 必须是布尔，存在时优先于 state 推断。`lease_owned()` 只检查登记的 lease、公开 generation、terminal epoch、未 unload，不看 `_ready`；保留旧 `lease_active()` 供需要可播放状态的调用。终止事件和本地 stop 清除 publisher 状态。

```python
def lease_owned(self, lease: MonitorLease) -> bool:
    return (not self._unloaded
            and self._leases.get(lease.lease_id) == lease
            and self._generation == lease.generation
            and lease.epoch == self._terminal_epoch)

@property
def publisher_running(self) -> bool | None:
    return self._publisher_running
```

- [ ] **步骤 4：运行 `python3 -m unittest tests.test_monitor -v`，再运行 `python3 -m unittest discover -s tests`；预期 PASS。**
- [ ] **步骤 5：运行 `git diff --check` 并提交。** `git add custom_components/doorfast/monitor.py tests/test_monitor.py`；提交 `fix(ha): separate monitor lease ownership from readiness`。

### 任务 2：首帧前跨暂时 stopping 的申请与取消

**文件：** 修改 `custom_components/doorfast/monitor.py`、`custom_components/doorfast/const.py`、`tests/test_monitor.py`。

- [ ] **步骤 1：编写失败的测试。** 已接受 `start` 后先收到同一 generation 的 `stopping`，随后 `requesting`、`publishing/ready=true`，单个 `async_acquire_viewer()` 应拿到有效 lease、只使能一次 viewer；两名同时申请者共享 generation。用可控时钟／`asyncio.wait_for` 测证申请超过旧 25 秒仍未被集成主动取消，随后 ready 可继续；页面取消或 HA 上层取消都使远端 stop 一次且不遗留无租约发布，终止 epoch 或 unload 后不再发新 start；与现有 `test_acquire_recovers_same_generation_after_stopping` 和 cancellation 测试并跑。

```python
first, second = await asyncio.gather(
    coordinator.async_acquire_viewer(), coordinator.async_acquire_viewer())
self.assertEqual(first.generation, second.generation)
self.assertEqual(2, coordinator.viewer_count)
```

- [ ] **步骤 2：运行 `python3 -m unittest tests.test_monitor -v`，预期新增时序测试失败。**
- [ ] **步骤 3：只在尚未拿到真实首帧时恢复。** `_wait_ready()` 将同一公开 generation 的 `stopping` 视为唤醒并交给 `async_acquire_viewer()` 的现有 `_recover_generation()`，不吞 `failed`、`monitor_preempted`、runtime/station 不匹配。集成调用 `async_wait_ready(generation, timeout=None)`，不因自身固定截止时间让仍打开的请求失败；`asyncio.CancelledError` 与外层异常共用 `_cleanup_cancelled_acquire()`，仅当自己启动且当前没有其他观看者时远端 stop，不影响已登记租约。HA 上层若自行取消请求，必须尊重取消。

```python
MONITOR_READY_TIMEOUT = None
async def async_wait_ready(self, generation: int,
                           timeout: float | None = MONITOR_READY_TIMEOUT) -> None:
    await asyncio.wait_for(self._wait_ready(generation), timeout)
```

- [ ] **步骤 4：运行 `python3 -m unittest tests.test_monitor tests.test_webrtc -v` 和完整 `python3 -m unittest discover -s tests`；预期 PASS。** 修正测试中旧的 25.0 timeout 断言，并验证 `async_wait_ready(..., timeout=None)` 不产生额外任务泄漏。
- [ ] **步骤 5：检查 diff 并提交。** 提交 `fix(ha): retain pending viewer through monitor retry`。

### 任务 3：WebRTC 协商重试分类与错误可见性

**文件：** 修改 `custom_components/doorfast/webrtc.py`、`tests/test_webrtc.py`。

- [ ] **步骤 1：编写失败的测试。** producer 尚未出现的 WebSocket `error`（仅 `not ready`／`streams: unknown error`）及 HTTP 404 重试且只持有一个 lease；WebSocket 其他 `error`、401/403 认证、错误 URL／配置、无效 WebSocket 消息、station 不存在、lease generation/epoch 改变时立即报明确 `HomeAssistantError`／`WebRTCError` 而不是无限重试。对 1 号现有的 answer 前 `unknown_error` 路径记录异常类型和不含凭据的 station/session 诊断日志；测试断言错误不被通用 `except Exception` 吞掉。关闭页面时 retry sleep 立即取消。

```python
with self.assertRaises(HomeAssistantError):
    await provider.async_handle_async_webrtc_offer(
        FakeCamera(source("gate_main")), "offer", "auth-failed", messages.append)
self.assertEqual(1, len(session.urls))
self.assertEqual(1, registry.monitors["gate_main"].released)
```

- [ ] **步骤 2：运行 `python3 -m unittest tests.test_webrtc -v`，预期当前泛化 retry 导致新增测试失败。**
- [ ] **步骤 3：实现 `_retryable_offer_error()` 分类。** 仅 `_ProducerNotReadyError`、等待 answer 超时、HTTP 404/503 可重试；401/403、非法消息、身份变化、lease 失效等直接抛给 HA。`_read_loop` 仅将 `not ready`／`streams: unknown error` 转为 `_ProducerNotReadyError`，其他协议错误转为 `HomeAssistantError` 并消费 answer future，避免后台异常丢失；日志只打印站点 ID、异常类、HTTP 状态及公开 generation，绝不打印 URL 凭据或 SDP。

```python
def _retryable_offer_error(error: Exception) -> bool:
    if isinstance(error, (_ProducerNotReadyError, asyncio.TimeoutError)):
        return True
    status = getattr(error, "status", None)
    return status in (404, 503)
```

- [ ] **步骤 4：在 offer 的 `except Exception as error` 中先清理本次 WebSocket但保留 lease，仅在 `_retryable_offer_error(error)` 且 `coordinator.lease_owned(lease)` 时 sleep/retry；否则抛出分类错误并释放 lease。** 对网络 `ClientConnectorError` 若要重试，显式加入其类型测试；不能再次用无条件 `except Exception` 重试。

```python
except Exception as error:
    if state is not None and not state.released:
        await self._cleanup_session(session_id, state, release_viewer=False)
    if not _retryable_offer_error(error) or not coordinator.lease_owned(lease):
        raise HomeAssistantError(f"Doorfast WebRTC offer failed: {type(error).__name__}") from error
    await asyncio.sleep(_NEGOTIATION_RETRY_DELAY)
```
- [ ] **步骤 5：运行 `python3 -m unittest tests.test_webrtc -v` 和完整测试，检查 diff 并提交。** 提交 `fix(ha): classify WebRTC offer failures`。

### 任务 4：answer 后只在真正终止时清理

**文件：** 修改 `custom_components/doorfast/webrtc.py`、`tests/test_webrtc.py`、`tests/test_setup_lifecycle.py`。

- [ ] **步骤 1：编写失败的测试。** 将 `tests/test_webrtc.py` 的 `FakeCoordinator` 扩展出 `lease_owned()`、`publisher_running` 与 `async_apply_status()`，遵守任务 1 的契约。answer 后同一 generation 的 `requesting/ready=false/encoder_running=true` 不关闭 WebSocket、不释放 lease；恢复 `ready=true` 后沿同一 WebSocket 接收 ICE；`failed`、preempt、generation 变更、`encoder_running=false` 或 go2rtc WebSocket 意外关闭时向 `send_message` 送明确 `WebRTCError`，关闭并只释放本 lease 一次；另一观看者与另一 station 保持有效。卸载、answer 前取消、迟到 `_read_loop` cleanup 与显式 close 并发时不得双释放。

```python
await registry.monitors["gate_main"].async_apply_status(
    {"generation": 9, "state": "requesting", "ready": False,
     "encoder_running": True, "status_revision": 8})
await provider.async_reconcile_monitor()
self.assertIn("main", provider._sessions)
self.assertFalse(main_ws.closed)
```

- [ ] **步骤 2：运行 `python3 -m unittest tests.test_webrtc tests.test_setup_lifecycle -v`，预期 ready=false 使旧 reconcile 关闭会话。**
- [ ] **步骤 3：修改 reconcile 与 reader 终止判据。** 先检查 registry 对象与 `lease_owned`、公开 generation、终止 epoch；同一发布的短暂 ready=false 不清理。若 `publisher_running is False`、明确终止事件或公开 generation 更替，用 `WebRTCError("doorfast_publisher_ended", ...)` 通知后清理；`_read_loop` 在 answer 后收到 WebSocket EOF／异常且不是显式 close 时也发同类错误。每个状态仅发一次错误；保留 `_cleanup_session` 的 owner 检查与 `cleanup_task` 串行机制。

```python
terminal = (coordinator is not state.coordinator
            or not coordinator.lease_owned(state.lease)
            or coordinator.generation != state.generation
            or coordinator.publisher_running is False)
if terminal:
    state.send_message(WebRTCError(
        "doorfast_publisher_ended", "Doorfast preview publication ended"))
    await self._cleanup_session(session_id, state)
```

- [ ] **步骤 4：运行 `python3 -m unittest tests.test_webrtc tests.test_setup_lifecycle -v` 和 `python3 -m unittest discover -s tests`；预期 198 项基线与新增测试全部通过。**
- [ ] **步骤 5：检查 `git diff --check`、核对仅本任务文件，并提交。** 提交 `fix(ha): retain answered WebRTC preview across source gaps`。

### 任务 5：跨仓联调与发布门槛

**文件：** 测试报告写入 Doorfast 工作树 `docs/superpowers/reports/2026-09-28-preview-continuity-ha.md`；仅实测揭示代码缺陷时修改对应 HA 测试与代码。

- [ ] **步骤 1：运行 HA 全套单元测试、Doorfast `make test` 与两边 `git diff --check`；检查工作树没有凭据、抓包或 token。**
- [ ] **步骤 2：按规格建立 PR，等两边 Actions 通过；先用 Doorfast PR 产物可回滚安装实机，HA HACS 更新由用户执行。** 不直接把本地未验证包推上实机。
- [ ] **步骤 3：每台至少十轮真实浏览器 ICE／解码。** 同时记录 Doorfast 公开与尝试代次、RTSP producer、`03/02`／`03/50`、真实解码帧、提示帧、首帧延迟、自动恢复及关闭清理。每台至少九轮 60 秒内有真实帧；设备主动结束后至少九成在同一浏览器会话 60 秒内恢复真实帧；关闭后 `active_sessions=0`、`active_encoders=0`。不满足则报告失败并继续修复，不合并／发布。
- [ ] **步骤 4：报告诊断结论。** 对 1 号 answer 前 `unknown_error` 记录精确异常类型／调用点，区分 Doorfast 设备忙、RTSP producer 和 HA 信令问题；不要把提示帧算作实时成功。报告只保存脱敏统计与 PCAP SHA-256，不保存 token／密码。
