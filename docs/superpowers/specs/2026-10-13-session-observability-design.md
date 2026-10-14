# Doorfast 会话结束原因、剩余时间与故障分类设计

日期：2026-10-13

## 目标

让 Doorfast 主程序和 Home Assistant 对每台门口机明确呈现：当前会话阶段、当前计时器及剩余时间、最近一次终止原因、当前或最近一次故障分类。该改动只补充可观测状态，不改变预览重试、通话超时数值、音频、门禁时序或媒体 ABI。

## 当前缺口

- Doorfast 已有 `call.session`、发送状态、确认状态、媒体 `state`、`last_error` 和预览阶段，但结束后缺少稳定的终止原因与门口机归属。
- Doorfast 已有会话 deadline、接听/呼叫确认 deadline 和媒体首帧 deadline；状态接口没有提供对用户有意义的剩余毫秒数。
- HA 已有 `requesting`、`awaiting_video`、`live`、`reconnecting`、`in_call`、`failed` 等预览阶段，但不能安全地区分“确认超时”“整个会话超时”“门口机主动挂断”“被通话抢占”和“媒体发布失败”。
- 旧主程序或旧事件缺少新增字段时，HA 必须显示 `unknown`，不能猜测为成功或失败。

## 稳定词汇

### 通话终止原因 `call.end_reason`

仅在通话或预览会话进入终止状态后记录最近一次结果；活动会话为 `none`。

| 值 | 含义 |
| --- | --- |
| `none` | 当前没有终止结果 |
| `local_hangup` | Doorfast/HA 主动挂断 |
| `peer_hangup` | 门口机主动结束 |
| `session_timeout` | 正式会话 deadline 到期 |
| `confirmation_timeout` | 保留词汇；当前 ACK 超时只设置 fault，不终止仍活动的会话 |
| `preempted` | 被更高优先级来电或正式通话抢占 |
| `runtime_reset` | daemon runtime 变化使旧会话失效 |
| `heartbeat_timeout` | 通话保活回复超过既有丢失门槛，仅用于 call |
| `unknown` | 已结束但没有足够证据分类 |

`confirmation_timeout` 不得被当作 `session_timeout`；控制确认和正式会话是两个计时器。确认窗口到期时现有会话仍可继续，`call.fault=confirmation_timeout`，`call.end_reason=none`；收到迟到、错代次或格式不符的 ACK 不等于明确业务拒绝。

### 故障分类 `call.fault` 与 `media.failure_reason`

故障值是稳定的机器可读枚举，状态接口同时保留已有数值错误字段供兼容调试。

| 值 | 使用范围 |
| --- | --- |
| `none` | 没有当前故障 |
| `dispatch_failed` | 控制帧发送失败 |
| `dispatch_timeout` | 控制发送事务达到重试/超时边界 |
| `confirmation_rejected` | 收到明确拒绝或不接受回复 |
| `confirmation_timeout` | 确认窗口结束仍无有效回复 |
| `first_frame_timeout` | 预览确认后未取得完整真实视频帧 |
| `source_stalled` | 已发布媒体超过停顿门槛没有真实源帧 |
| `encoder_failed` | 编码器启动或运行失败 |
| `publisher_failed` | RTSP/go2rtc 发布失败 |
| `capacity_busy` | 编码容量或门口机忙 |
| `preempted` | 被通话或更高优先级会话抢占 |
| `cancelled` | 用户或生命周期取消 |
| `runtime_reset` | runtime 变化清理旧资源 |
| `heartbeat_timeout` | 通话保活超时，仅用于 call.fault |
| `unknown` | 无法安全归类 |

### 计时器字段

活动对象提供：

```json
{
  "timer": "none|confirmation|session|first_frame|reconnect",
  "remaining_ms": 0
}
```

`remaining_ms` 是 Doorfast 在生成该状态快照时计算的相对值，永远不输出主机 monotonic deadline；已知无活动计时器为 `timer=none`、`remaining_ms=0`。未知或缺少可选诊断扩展时 `timer=unknown`，省略 `remaining_ms`，不能伪造零。确认计时器到期只清除该计时器并记录 fault，正式会话可继续使用自己的计时器。

持久预览的 `requesting` 只有重复请求间隔，没有总确认 deadline，因此输出 `none/0`；真实等待首帧使用 `first_frame`，现有恢复、冷却或停止重试等待使用 `reconnect`。

## Doorfast 状态契约

在现有 `call` 对象中增加：

- `timer`
- `remaining_ms`
- `end_reason`
- `fault`
- `last_station_id`
- `last_generation`

在每个 `media.sessions[]` 项中增加：

- `timer`
- `remaining_ms`
- `end_reason`
- `failure_reason`

`media.sessions[]` 保持只包含当前会话。诊断记录按 `station_id` 和公开 `generation` 同时匹配；历史结果不能填充活动会话的原因或剩余时间。新增 `media.last_results[]` 独立保留每个已配置 station 最近一次真实终止结果，包含 `station_id`、`generation` 和上述四个诊断字段，终止计时器为 `none/0`。容量拒绝未获得有效 generation 时仍只保留已有数值错误，不制造终止记录。

字段由主程序根据真实状态机和媒体模块结果生成。不得从 HA 请求、前端状态或历史事件反推。新增字段通过独立可选 diagnostics v2 扩展提供，不改变媒体 ABI v3 或 diagnostics v1 大小和布局；旧扩展仍可提供启动计数，新原因降级为 `unknown`。事件推送中的 `status` 使用相同字段。新 runtime 的通话和媒体历史为空，不跨进程持久化或制造 `runtime_reset` 终止结果。

### 阅读当前诊断

例如当前 `gate_main` generation 9 等待首帧时，`media.sessions[]` 可输出 `timer=first_frame`、`remaining_ms=900`、`end_reason=none`；同 station generation 8 的主动停止只出现在 `media.last_results[]`。LuCI 分别展示当前和最近结果，倒计时标为最近采样的相对毫秒值，未知诊断不影响已有服务状态展示。

当前已实现的媒体分类包括首帧超时、源停顿、容量忙、编码器失败、抢占和取消。通用 ffmpeg 失败无法证明发布器失败，使用已有证据支持的 `encoder_failed` 或 `unknown`；`publisher_failed` 仍是保留词汇，并无已验证的独立生产者。忙和源停顿可出现在恢复中的当前会话上，不强制变为终止失败。其他保留枚举也不代表已有生产者；不充分证据保持 `unknown`。

capture-error 的既有清理会结束当前通话；主程序在清理后观察这次真实转换，保留原 station 和 generation 并记录 `end_reason=unknown`。这不等于 runtime restart，也不添加新的中止或控制动作。

## HA 展示契约

- 每台门口机的预览状态传感器保留现有 enum 状态；属性增加 `monitor_timer`、`monitor_remaining_ms`、`monitor_end_reason`、`monitor_failure_reason`、`monitor_last_generation`。
- 控制器状态传感器增加当前通话的 `call_timer`、`call_remaining_ms`、`call_end_reason`、`call_fault`、`call_last_station_id`。
- 翻译文件为英文和简体中文提供稳定枚举文案；未知或旧字段显示 `unknown`，不显示空字符串冒充空闲。
- 事件推送和 5 秒轮询使用同一归一化函数；事件丢失后刷新不能回退到旧 generation/revision。
- 任何密码、令牌、原始媒体字段、地址材料和 SDP 均不得进入新增属性。

## 不在本次范围

- 不修改 03/04、03/50、03/02、03/82 控制节奏。
- 不修改音频接收、播放、G.711 上行或全双工逻辑。
- 不新增随机挑战开锁算法。
- 不把 `ready=true` 解释成真实视频在线。
- 不修改 LuCI 配置写入流程或用户仪表盘布局。

## 测试与验收

### Doorfast

- 会话：本地挂断、门口机挂断、正式会话超时、保活超时、抢占得到真实终止原因；确认超时只记录 fault，迟到回复不覆盖历史，新 runtime 历史为空。
- 计时器：确认计时器与会话计时器分别倒计时；活动时剩余值单调减少；终止后清零。
- 媒体：首帧超时、忙、源停顿、编码失败、停止和抢占按证据映射；未独立证明的发布失败保持未知边界，旧媒体错误数值保持兼容。
- JSON/CGI/LuCI 状态测试覆盖新增字段、旧字段缺失、generation 隔离和敏感字段过滤。

### HA

- 新旧 bridge 字段归一化；所有未知枚举安全降级。
- 事件与轮询相同结果；旧 generation、旧 revision、迟到结束事件不能覆盖新会话。
- 三台 station 的 requesting/awaiting/live/reconnecting/in_call/failed 及终止属性测试。

### 现场

先使用通过 CI 的包，在 1、2、B2 各执行一次预览首帧超时或短流恢复、一次真实通话结束和一次正常停止，确认状态字段与 Doorfast 日志一致；不以状态字段替代真实视频/音频验收。
