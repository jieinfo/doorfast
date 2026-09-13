# GVS 电梯召梯与状态协议设计

更新：2026-09-13。本文定义 Doorfast 主机模式的首个电梯协议增量。它只覆盖已由
MT8157 静态材料或现场 PCAP 支持的 `08/02`、`08/03`、`08/82` 和 `08/83`，不实现
楼层选择，也不解释语义尚未闭合的 `08/01`。

## 目标与证据边界

本阶段交付可独立测试的电梯协议和事务模块，为后续真实 UDP、ubus 和 Home Assistant
接入提供稳定边界。模块可以报告“请求已发送”“协议完成”“超时”等事实，但不得把
`08/82` 或本地发送成功描述为电梯已经到达。

证据来自用户指定的厂商交付数据集：

| 证据 | 类型 | 支持的结论 | 限制 |
|---|---|---|---|
| `docs/Snippet/015-elevator.md` | 静态调用链汇总 | 操作码、目标地址、定时器、状态数组布局 | 汇总可能存在转述错误 |
| `docs/doorfast-disconnect-20260911.pcap` | 现场网络记录 | 5 个 `08/02`、2 个 `08/03` 的实际线上布局 | 没有 `08/82` 或 `08/83` |
| `docs/gvs-compatible-host-development-manual.md` | 兼容设计导航 | 模块边界与身份格式 | 关键字段必须回溯到静态或 PCAP |
| Doorfast 主机测试 | 项目验证 | 构帧、解析、计时与错误边界 | 不能证明实体设备接受或动作 |

PCAP 的 SHA-256 为
`c2a1ad01474665c430232e86083cd939ef2aa5c9fad28e6856ffb85a506ad2a7`。
本设计采用普通协议设计结构（`flavor=null`），不套用恶意软件或漏洞报告结构。

### 静态材料差异的处理

静态汇总将 `08/02` payload 写为
`direction, BCD(g[3]), g[3], g[4]`。现场样本给出了更精确的含义：

```text
source  = 61:02:01:16:01:01
payload = 00 10 16 01
```

源地址的 `g[3]=0x16` 是 packed BCD 的 16 层，payload 第二字节 `0x10` 是十进制
16 的普通二进制表示，第三字节保留原始 BCD `0x16`。另外四个 `g[3]=0x06` 样本中
两者相同，无法单独揭示转换。Doorfast 因此明确使用：

```text
payload[0] = direction (0=down, 1=up)
payload[1] = decode_packed_bcd(local[3])
payload[2] = local[3]
payload[3] = local[4]
```

非法 BCD、解码楼层超过 99、非室内机来源或零地址均拒绝构帧。此规则同时满足全部
五个 PCAP 样本，并纠正了静态汇总中容易被理解为再次 BCD 编码的歧义。

同一静态汇总还把 `08/02` 总长记为 48 字节，但其字段定义为 42 字节公共头加 4 字节
payload，五个 PCAP 样本也全部为 46 字节。Doorfast 采用逐字节证据支持的 46 字节，
不为满足汇总中的算术错误补入两个零字节。

## 方案选择

评估过三种组织方式：

1. 直接把电梯分支写进 `runtime_service.c`。改动少，但协议、计时和网络副作用耦合，
   无法用 PCAP 黄金样本单独验证。
2. 新增专用 codec 与单槽事务控制器，再由现有 UDP 和 ubus 层适配。该方案沿用通话
   和门禁模块的边界，也能保留电梯独有的地址及重试规则。
3. 先抽象通用业务事务框架。它会迫使门禁、通话和电梯共享尚未证明一致的确认语义，
   当前证据不足以支持这种抽象。

采用方案 2。首个实现 PR 只增加协议构帧、解析和纯计时控制器；第二个 PR 再接运行时
UDP 与受控本地接口。这样可以先审查协议规则，也避免模拟测试意外发送网络数据。

## 协议模块

新增 `src/gvs_elevator.h` 和 `src/gvs_elevator.c`，提供以下职责：

- 从六字节本机室内机身份生成目标 `35 g[1] g[2] 00 01 00`；
- 构造方向仅为 0 或 1 的 `08/02`，使用 4 字节 payload 和厂商公共头提供器；
- 构造零 payload、总长 42 字节的 `08/03` 状态查询；
- 识别严格反向地址的 `08/82`，不解释未知 payload；
- 有界解析 `08/83` 的数量和楼层/状态二元组。

请求结构保存目标、来源、方向和四字节 payload。序列化仍调用
`df_gvs_control_serialize`，因此公共头生成失败时输出长度归零，调用方缓冲区保持不变。

`08/83` 最多解析 8 部电梯。payload 必须至少包含 `1 + 2 * count` 字节；多出的字节
记录为 `extension_length`，不赋予语义。楼层先按 Java signed byte 还原，再复刻旧实现：
非负值原样保留；负值转换为 `-(raw + 0x80)`。状态保留原始值，同时只为已知的
`0=fault`、`1=up`、`2=down`、`3=stop` 提供显示名称，其他值为 `other`。

## 召梯事务

控制器任一时刻只有一个活动召梯事务。提交时绑定本地 transaction id、目标、来源、
方向和单调时间；transaction id 只在本机使用，不进入线上帧。

状态转换如下：

| 状态 | 进入条件 | 后续行为 |
|---|---|---|
| `idle` | 初始化 | 可提交 |
| `waiting` | 提交通过且仍有发送机会 | 1000 ms 时发送第二次；记录每次发送结果 |
| `protocol_completed` | 等待期间收到匹配 `08/82` | 停止计时；物理结果仍未确认 |
| `expired` | 2000 ms 到达且未收到匹配回复 | 停止，不继续重试 |
| `send_failed` | 两次发送机会均失败 | 停止 |
| `cancelled` | 身份、目标或服务生命周期失效 | 停止 |

首次提交立即产生发送动作，并进入 `waiting`；1000 ms 产生第二次也是最后一次发送
动作；2000 ms 结束事务。这对应厂商计时器的 `count=1`、`count=2` 发送和 `count=3`
失败回调。单次发送失败不会提前结束，只要仍有下一次发送机会；两次都失败时第二次
返回后立即进入 `send_failed`。至少一次发送成功但没有回复时，在 2000 ms 进入
`expired`。等待时重复提交一律拒绝，不改变原事务计时或方向。

`08/82` 当前没有现场样本，控制器只校验 family、opcode、严格反向地址和活动时间窗，
不要求特定 payload。状态名使用 `protocol_completed`，并固定输出
`physical_result_confirmed=false`。迟到、错误来源、错误目标和非活动事务回复均忽略。

## 状态查询边界

首个协议 PR 只提供一次 `08/03` 构帧和 `08/83` 解析，不实现周期调度。后续运行时
接入时，状态查询使用独立单槽，不与召梯事务共享完成条件。上层可按约 1 秒请求，
但必须保证任一时刻最多一个未完成查询，并限制连续失败，避免对未知设备持续发包。

状态响应不会作为召梯成功证据。HA 或 LuCI 最终只能显示已解析的电梯数量、楼层、
运动状态、数据年龄和协议验证级别。

## 运行时与接口接入计划

协议模块通过测试后，下一 PR 将：

- 为 `gvs_udp_sender` 增加电梯发送适配，复用观察到的对端 IP 路由；
- 仅在 `active_host` 模式开放 `call_elevator`，参数为方向，不接受任意 payload；
- 状态公开事务状态、方向、尝试次数、原始状态值和数据年龄，不公开完整私有帧；
- 被动模式、无效身份、旧事务、重复请求和未配置电梯能力均拒绝发送；
- 使用厂商 PCAP 黄金样本、UDP 回环和 ImmortalWrt APK 验证后再合并。

活动通话不作为召梯前置条件。厂商发送路径以本机身份和电梯节点为目标，并未
把召梯绑定到门口机通话 generation；强行绑定会阻止独立召梯。运行时仍以本地
transaction id、防重复槽位和 active-host 门控控制并发。

## 测试与验收

协议测试至少覆盖：

- `0x16` BCD 楼层产生 `0x10 0x16` 两个 payload 字节；全部五个 PCAP 请求可重建；
- 上下行、目标地址、42/46 字节长度和厂商公共头提供器失败原子性；
- 非法 BCD、方向、来源类型、目标与缓冲区容量；
- 立即发送、1000 ms 第二次发送、2000 ms 到期及倒退时间；
- 重复提交、一次/两次发送失败、匹配与迟到 `08/82`；
- 空、截断、超量、扩展字节、负楼层和未知状态的 `08/83`。

目标系统验证分为三层：主机测试证明代码行为；ImmortalWrt UDP 回环证明目标工具链、
ubus 和线上布局；用户自有电梯现场验证证明设备接受与物理动作。只有最后一层通过后，
产品状态才能从“协议已发送/完成”升级为“现场已验证”。

## Evidence → Finding → Path

### E-001

- title: 电梯静态调用链与计时器记录
- observed_at: 2026-09-13
- source_type: file
- source_ref: 厂商数据集 `docs/Snippet/015-elevator.md`
- content_hash: n/a（厂商数据集不随公共仓库提交）
- artifact_path: n/a
- repro_command: `sed -n '1,180p' /Users/shenwenjie/Documents/PVE/mt8157/docs/Snippet/015-elevator.md`
- raw_excerpt: `08/02` 四字节、`08/03` 零字节；0/1000 ms 发送，2000 ms 失败结束
- linked_workitem: n/a
- supersedes: none

### E-002

- title: 现场电梯帧逐字段复核
- observed_at: 2026-09-13
- source_type: network
- source_ref: 厂商数据集 `docs/doorfast-disconnect-20260911.pcap`
- content_hash: `c2a1ad01474665c430232e86083cd939ef2aa5c9fad28e6856ffb85a506ad2a7`
- artifact_path: n/a
- repro_command: `tcpdump -nn -XX -r /Users/shenwenjie/Documents/PVE/mt8157/docs/doorfast-disconnect-20260911.pcap 'udp port 8300'`（需要本地厂商数据集；按 GVS 偏移 38/39 筛选 `08/02`、`08/03`）
- raw_excerpt: 5 个 46 字节 `08/02`、2 个 42 字节 `08/03`；`61:02:01:16:01:01` 对应 `00 10 16 01`
- linked_workitem: n/a
- supersedes: none

### F-001

- title: `08/02` 第二字节是 BCD 楼层解码后的普通二进制值
- severity: n/a_re
- category: reverse_algo
- status: validated
- evidence_ids: [E-001, E-002]
- location: `08/02 payload[1..2]`
- impact: 若把 `0x16` 再次编码为 BCD，会为 16 层生成错误请求
- confidence: high
- repro_steps: 对照 PCAP 中来源 `g[3]=0x16` 与 payload `0x10 0x16`
- remediation: 先校验 packed BCD，再解码到普通二进制，并保留原始 BCD 字节
- optional_attack: n/a

### F-002

- title: `08/82` 和 `08/83` 仍缺现场响应样本
- severity: n/a_re
- category: other
- status: candidate
- evidence_ids: [E-001, E-002]
- location: GVS family `0x08`
- impact: 本地协议完成和状态解析不能证明目标电梯支持或已经动作
- confidence: high
- repro_steps: 扫描 PCAP 的 family/opcode 计数，响应计数均为零
- remediation: 保留原始状态和证据等级，等待用户自有设备闭环
- optional_attack: n/a

### F-003

- title: `08/02` 线上总长为 46 字节
- severity: n/a_re
- category: reverse_algo
- status: validated
- evidence_ids: [E-001, E-002]
- location: GVS `08/02` datagram
- impact: 按静态汇总的 48 字节发送会增加两个未经证实的尾随字节
- confidence: high
- repro_steps: 核对公共头 42 字节、声明 payload 4 字节，并读取五个 PCAP 样本实际长度
- remediation: 序列化固定输出 46 字节，并用五个现场样本做黄金测试
- optional_attack: n/a

### P-001

- title: 从本机身份到有界召梯事务
- path_type: callflow
- start: 已校验的 GVS 室内机六字节身份和方向
- goal: 可审计的召梯协议结果
- steps:
  1. 解码 BCD 楼层并生成 46 字节 `08/02` — evidence: E-001, E-002 — finding: F-001, F-003
  2. 立即发送并在 1000 ms 最多重发一次 — evidence: E-001 — finding: none
  3. 匹配 `08/82` 或在 2000 ms 结束事务 — evidence: E-001, E-002 — finding: F-002
- residual_risks: 响应字段和实体动作仍需现场验证；`08/01` 保持未知
