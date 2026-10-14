# 主动呼叫：预览确认、真实会话归属与失败清理

## 目标和实施边界

修复 HA 对指定门口机发起主动呼叫时，媒体被标记为 `call`，但真实控制会话仍是 `idle` 的问题。用户已同意将“先确认预览，再升级通话”纳入修复。本规格是设计，不代表代码修复或实机验收已经完成。

本设计补充 [独立主动呼叫规格](2026-09-30-station-active-call-design.md)，落实其原有 `03/04 → 03/84 → 03/03 → 03/83` 顺序，并澄清控制 generation 与媒体 generation 的不同归属。

- 继续使用现有 Doorfast 和 HA 工作树，不重建、重置、清理或 stash。
- 不改变来电自动化、门禁时序、音频算法、麦克风安全上下文要求或用户仪表盘。
- 不改变普通预览的请求频率、忙重试阈值、停止冷却或短流恢复策略。
- 保留现有 HTTP 路径、按钮、服务和媒体 ABI v3 的结构布局、函数表；不增加猜测性的 GVS 帧。
- 不在本修复中扩展端口或 duration 参数配置。现有 station call 使用视频 8303、音频 8302 和 120 秒通话时长；CGI 对这些 HA 参数的既有处理不作为新能力宣称。
- 本地实现、审查和验证与推送、PR、部署、现场主动呼叫分开。后者按用户后续授权执行，不轮询 Actions。

## 已验证的断点

代码基线：Doorfast `5bd3de524e986525848d8e9a8f50077cbfb6c59d`；HA `220eecdb9199aaa55d8634e65698bc2eb23d396e`（v0.3.27）。

| 证据 | 当前结果 | 修复要求 |
| --- | --- | --- |
| 实机通过 HA 对 1 号发起一次主动呼叫，观察约 12 秒 | 媒体为 `purpose=call`、generation 8；主会话持续 `idle`、generation 0；调度 `cancelled`、发送次数 0 | 请求排队和媒体分配不得冒充已建立通话 |
| [runtime_service.c](../../../src/runtime_service.c) 的 `df_runtime_call_submit` | 在栈上复制主会话，伪装为 `PREVIEW` 后提交；真实主会话未改变 | 调度、ACK、保活、音频必须引用同一个真实控制 owner |
| [runtime_media_module.c](../../../src/runtime_media_module.c) 的 `df_runtime_media_module_call` 及 [media_session_manager.c](../../../src/media_session_manager.c) 的 CALL start | 主动呼叫复用来电分配路径，直接 bind call monitor，没有先完成目标预览确认 | 区分主动升级与来电媒体绑定，不提前破坏已有预览 |
| [runtime_ubus.c](../../../src/runtime_ubus.c) 的 `df_runtime_ubus_hangup_station` | 将媒体 generation 直接用于主会话挂断；媒体清理成功后仍可能返回提交错误 | 精确映射控制身份；清理完成、排队成功和物理确认分别报告 |
| HA `custom_components/doorfast/station_status.py` | 仅凭 `purpose=call` 显示 `in_call` | 仅真实、匹配目标的 `talking` 状态允许显示通话中 |

上述实机证据仅来自 1 号主动呼叫，不代表三台均已完成主动通话测试，也不证明出站双向语音成功。

## 方案选择

采用运行时主动呼叫协调器：复用媒体模块的预览准备和现有主控制状态机，补上两者之间的操作归属、升级门控与清理。

| 方案 | 代价与边界 | 选择 |
| --- | --- | --- |
| 只把临时 PREVIEW 写回主会话 | 能消除本次调度身份不匹配，但仍跳过预览确认，也没有跨 generation 的清理归属 | 不作为完整修复 |
| 运行时协调既有媒体与控制状态机 | 保留现有 ABI 和协议，集中管理一次主动呼叫的异步准备、确认和资源归属 | 采用 |
| 将正式通话控制全部迁入媒体模块 | 需要重构来电、音频、门禁引用和模块接口，扩大已验证链路的变动面 | 本次不实施 |

协调器只保留一个当前/最近终态操作，不引入无限操作队列、持久化呼叫记录或新配置项。

## 会话身份与职责

### 三类身份不混用

| 身份 | 所有者 | 用途 |
| --- | --- | --- |
| `runtime_id + operation_id` | 主动呼叫协调器；operation_id 在一次进程运行中单调增长 | 去重、异步结果归属和取消；不是 GVS 帧中的新字段 |
| `runtime_id + station_id + call_generation` | 真实主 `df_gvs_session` 与现有控制状态机 | `03/03` / `03/83`、挂断、保活、正式通话倒计时和音频身份 |
| `runtime_id + station_id + media_generation` | 媒体会话管理器 | 预览、视频发布、viewer 和媒体资源停止 |

- 等待预览时 `call_generation=0`，不得伪造 `talking` 或占用音频身份。协调器预留的是主动呼叫准入，不是第二个正式控制会话。
- 正式升级使用主控制会话自己的单调 generation，不将媒体 generation 赋给它，不回退计数。溢出时拒绝新操作，保留旧状态。
- `POST /api/v1/call` 的既有 `generation` 继续表示媒体 generation；新增字段明确给出 operation 与 control 身份，不能悄悄重解释原字段。
- 主会话、媒体会话、操作记录的 station 及 runtime 必须一致。旧 runtime、其他 station、旧 operation 或失效媒体 key 均不能推动当前操作。
- GVS 回复本身没有可任意注入的新 operation_id。迟到包只能通过现有地址、载荷、发送窗口、当前 monitor/ACK 归属及冷却检查约束；不承诺线上同地址同载荷的旧包具有不存在的 generation 标记。

### 各组件的职责

- HTTP/ubus：验证身份与准入，返回异步排队结果，不阻塞等待门口机，不在排队前直接创建 CALL 媒体。
- 运行时协调器：绑定目标、记录借用/自建预览、推进有界准备、提交真实控制命令，并统一终态清理。
- 媒体模块：执行现有预览和精确确认检查；主动升级必须由模块自己的当前 monitor 确认状态门控，不能只相信运行时看到一个 `03/84` 原始帧。
- 主控制状态机：保留原调度 current 检查、ACK 校验、保活及正式通话 deadline。不能放宽调度身份保护来“修复”取消。
- HA：读取同一份权威快照，展示呼叫阶段；媒体就绪、源在线和通话建立是不同事实。

## 呼叫和停止流程

### 准入与预览准备

1. 校验 runtime、目标 station、enabled、monitorable、路由、媒体能力和全局通话占用。其他 station 已通话或存在另一个主动呼叫操作时返回 `busy`，不触碰该 station 的预览。真实主会话的 `PREVIEW/RINGING/TALKING` 若不能证明属于当前操作，也不得被新出站 owner 覆盖；普通媒体预览不等于占用这个主 owner。
2. 同一 station 的重复呼叫在准备、升级或通话期间返回同一操作身份，不再次发送新的升级命令。主会话已经属于该 station 的正式来电通话时只返回现有通话状态，不转换来电归属。
3. 若目标已有有效预览，借用其精确媒体 key，并记录 `owns_preview=false`；否则经既有 preview start 创建，记录 `owns_preview=true`。失败准入不能停止用户的已有预览。准备期间若用户取得同一预览的 viewer，则转为共享预览，取消呼叫不能再按独占自建预览销毁它。
4. 操作进入 `preparing`，整体准备预算为 30 秒，使用单调时间；此预算不是普通预览重试策略的修改。预算内保留现有忙重试和冷却。
5. 只有该预览当前 monitor 已接纳匹配的 `03/84`、未进入重试/停止/终态且未超时，才允许升级。有效且仍处于这一窗口的既有预览可以复用确认；历史 `confirmed_ms`、`ready=true` 或 `source_live=true` 单独均不是升级凭证。
6. 等待期间收到来电、网络丢失、runtime 结束、预览被停止/替换或预算到期，取消本操作。来电继续沿用既有处理和自动化，不能被主动呼叫覆盖。

### 正式升级与确认

1. 利用媒体模块已有 `start(... CALL, 0, now)` 参数分支，实现有门控的出站原地升级语义，而非继续复用 incoming call。该路径必须要求有效预览确认；非零 request generation 的现有来电绑定保持独立，不改变 ABI 结构和函数表。
2. 原地升级保留 public media generation、视频 encoder/publisher、队列与 viewer 归属，不另建视频会话。内部预览控制与正式通话控制的职责交接须禁止两个保活 owner 同时发包。
3. 为同一目标创建真实主 `PREVIEW` 控制 owner，以主会话的新 generation 排队 `03/03`。媒体升级与控制排队视为一个准备事务：排队失败不得留下假的 CALL 或半安装 owner；无 UDP 发送前，借用预览回滚到原状态，自建预览按既有停止冷却清理。
4. 操作进入 `calling`；既有调度器发出 `03/03` 后才开启既有 1,000 ms 确认窗口。发送失败/超时和确认超时均结束本次升级，不无限重发正式呼叫。
5. 只有既有 ACK 校验确认匹配的 `03/83` 并使真实主会话变为 `TALKING`，操作才进入 `talking`。正式 session timer 和音频沿用真实主会话身份与既有逻辑。
6. 相邻准备事务不得把过期 ACK 或上一次媒体 key 重新绑定给新通话。正式通话期间，media CALL 状态与控制状态不一致或媒体故障也必须进入结束清理，而非一直保留假通话。

### 取消、失败与挂断

- 准备阶段取消：停止本操作拥有的自建预览；借用的用户预览保持原状态，不给其他站发送挂断。
- 升级命令尚未发送的失败：取消精确调度项，回滚未发送的交接。不得在下一轮主循环重新发送已取消命令。
- 升级已发送后的失败、用户挂断、peer 挂断、session/heartbeat timeout：由真实控制 owner 负责结束。需要本地挂断时只由控制路径发送 `03/02`；媒体释放不再独立重复发送同一次正式挂断。
- 显式 station hangup 通过操作映射选择正确控制 generation 与媒体 key，不能直接将传入媒体 generation 当控制 generation。兼容旧全局挂断入口，仍只作用于当前真实主会话。
- 本地挂断等待匹配 `03/82` 或既有确认超时；结束必须释放音频、保活与精确媒体资源。HA 在成功提交挂断后释放匹配该通话的收听/麦克风所有权；主程序按真实本地结束路径清理音频，最迟在有界结束窗口内完成。不将“已排队”表述为“室外机已确认挂断”，也不为提前停止音频伪造主会话状态。
- 已终止的同一操作重复 hangup 返回已结束/idle，不再报无效主会话；已配置但无活动呼叫的 station 返回 idle，不能影响另一 station。旧 runtime、非法/未知 station 或显式失配 key 仍返回错误，不能吞掉真实发送或清理失败。
- 普通 monitor/viewer 的关闭或延迟 stop 不能销毁准备中的呼叫预览或正式 CALL；拒绝越权 stop，并让 HA 释放该 viewer 自己的引用。显式 call hangup 才取消呼叫或结束正式通话。
- 调用前借用的预览在正式呼叫已经发送后也属于受影响的同一物理会话：通话结束时不承诺仍为 live。HA 对后续预览使用既有重新获取和冷却机制，不修改短流恢复或假装视频连续。
- 来电接管、media key 被替换或 generation 变化时，清理仅作用于旧操作仍拥有的资源；不得停止新来电媒体、覆盖新主会话或删除新通话音频。

## 状态契约与 HA 展示

保持 `call.session`、`call.generation`、`call.timer`、`call.remaining_ms`、故障与终态字段的现有语义。准备阶段主会话可能仍为 `idle`；不能为了 UI 改写这些事实。

在 HTTP/ubus status 中新增有界 `station_call` 对象，单独暴露主动操作。以下字段属于新增契约，不是当前版本已经存在的字段：

| 字段 | 类型/值 | 语义 |
| --- | --- | --- |
| `operation_id` | uint64；idle 为 0 | 当前或最近操作序号；与顶层 runtime 一起使用 |
| `station_id` | 已验证 station ID；无操作为空 | 操作目标 |
| `stage` | `idle/preparing/calling/talking/ending/ended/failed` | 当前主动呼叫阶段 |
| `media_generation` | uint64；无绑定为 0 | public 媒体身份 |
| `call_generation` | uint64；未安装 owner 为 0 | 正式控制身份，不与媒体 generation 比较大小或等值 |
| `timer` | `none/startup/confirmation/session` | startup 是主动操作的整体准备预算；后两者复用主控制 timer |
| `remaining_ms` | uint64 | 对应 timer 的本次采样剩余量；终态必为 0 |
| `error` | `none/busy/station_unavailable/ack_timeout/dispatch_failed/dispatch_timeout/media_failed/preempted/cancelled/runtime_reset/unknown` | 稳定、受白名单限制的操作结果，不含原始错误正文或秘密 |

CALL 和 station HANGUP 回复保留既有 `queued`/`generation`，增加 runtime、station、operation 与 `state`。HTTP 接受排队不等待现场完成，后续失败从 status 读取，不通过已完成请求的 HTTP 状态补报。

Station HANGUP 增加可选 `operation_id` 条件：提供时必须匹配同一 runtime/station 的当前或最近已结束操作；失配返回结构化冲突，不挂断更新的操作。新 HA 在取得出站操作身份后携带该条件，CGI/ubus 必须完整验证并传递。旧调用者只传 station 的行为仍是“停止该 station 此刻的呼叫”；它不能提供不存在的旧操作保护。来电挂断继续使用其真实控制身份，不强制要求出站 operation_id。

- HA 新增预览状态枚举 `calling`，对应同一有效操作的 `preparing/calling`；提供英文和简体中文文案。`ending` 对应既有 `stopping`。
- `in_call` 必须有该目标真实主会话 `talking` 和正数控制 generation；对新版出站操作还必须匹配其 control/media 映射。匹配来电 TALKING 继续支持，不要求它有出站操作记录。
- 没有新增操作对象的旧 Doorfast，HA 可以按匹配的真实主会话显示通话，但不能仅凭 `purpose=call` 推断已接通。缺失、歧义或身份冲突不得自动挑第一台 station。
- `source_live`、publisher readiness 和首帧时间继续取媒体事实。通话中不等于视频当前在线；这些字段不得被强制设为 true。
- 在现有 station sensor 属性中暴露经过验证的操作阶段/身份/error/timer，不新增重复实体。终态操作作为历史属性，不覆盖一个仍存在的有效普通预览阶段。
- HA 服务/按钮/MonitorCoordinator 共用权威状态推导；响应 queued 只表示 calling，不能提前进入 talking。新 runtime、旧 operation、陈旧状态 revision 不得回退或串台。
- 音频 AudioIdentity 继续只接受真实 `call.session=talking` 和控制 generation；本阶段不放宽 HTTPS、麦克风权限或音频 admission。

## 测试与交付判据

实现采用 TDD；每条新行为先在真实生产路径上产生预期失败，再以最小修改通过。UDP、时钟与 encoder 外部边界可以受控替换，运行时 owner、调度、ACK 和 HA 状态消费不能用“总是成功”的假回调代替。

### Doorfast 本地验证

- 复现真实 ubus/运行时提交到调度的断点，断言发送次数、目标与真实主状态，不能只测 command builder。
- 无预览时先发 `03/04`；错误 station/IP/载荷、过期确认或重试中的 `03/84` 均不允许 `03/03`。有效当前确认才升级。
- 已有确认预览原地升级，保留 publisher、media generation 与 viewer；未确认预览只等待；重复 call 不重复提交。
- 分别使用不同的 media/control generation，验证 ACK、音频、station hangup、全局 hangup 和终态身份正确。
- 控制排队失败、发送失败、确认超时、准备超时、媒体失败、peer 挂断和主 session/heartbeat timeout 均有有界终态与无残留资源。
- 自建/借用预览的取消和事务回滚不同；不能破坏借用预览或其他 station。
- 来电抢占、网络丢失、runtime 停止和旧 key 清理不污染新 owner；普通 viewer stop 不能结束正式通话。
- 重复挂断成功，不把错误 runtime/station/key、真实发送失败或清理失败当成功。
- 全量主程序、CGI、模块 ABI/包清单与既有 LuCI 测试通过；明确报告任何基线警告，检查最终 diff。

### HA 本地验证

- 覆盖 preparing/calling/talking/ending、失败后保留普通预览、旧主程序缺失操作对象及来电 TALKING 的状态矩阵。
- `purpose=call` 加主 `idle`、另一 station TALKING、失配 generation、歧义媒体列表和 malformed 操作对象都不能显示 `in_call`。
- 验证真实 sensor/coordinator 消费、按钮/服务返回处理、中英翻译、runtime/revision 更新和音频身份不被放宽。
- 全量 Python、Node、JSON/compile 和 diff 检查通过；本地通过不等于 SDK 或现场通过。

### 后续现场验证

用户授权部署后，分别通过 HA 对三台门口机执行受控主动呼叫/保持/挂断，同时采样元数据与 8300/8302/8303 控制及媒体计数。成功必须观察实际预览确认、实际升级发送、匹配 ACK、真实 TALKING、正确目标音视频流和无残留资源；一次失败也应有明确终态和可再次呼叫的状态。

现场测试不解锁门禁、不修改自动化、不保存原始语音或令牌。浏览器画面、听感、麦克风、回声和延迟分别由现场证据验证，不能仅凭 UDP 包、队列成功或 synthetic fixture 宣称出站全双工验收完成。

## 下一步

本规格经用户审查后，为 Doorfast 协调器/媒体交接、HTTP/ubus 契约、HA 状态消费和集成验证编写实现计划，再进入测试驱动开发。本轮只提交设计文档，不推送、创建 PR、安装包或发起新的实体呼叫。
