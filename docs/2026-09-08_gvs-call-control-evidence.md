# GVS 来电控制面证据记录

> 复核更正（2026-09-08）：下文关于 F-001、P-001 及表格中
> “0x81 为来电入口”的结论已撤回，作为历史记录保留，不再作为实现依据。
> c.smali 的 a([B,String,int) 写入 0x81 并记录 sendCallReply；
> TalkBackBusiness 待机下的 0x81 分支调用的是协议发送包装函数，
> 不能由此推断进入铃响状态。普通来电分支是操作码 0x01。
> c.smali 的字符串类型分发表将 IS 映射到 pswitch_2（0x61），
> DOORSTATION 映射到 pswitch_3（0x32）；旧版 Doorfast 将二者混淆。
> 当前代码已修正 IS 类型，并将 0x81 保留为 Unknown，0x01 映射为来电候选。
> 下面旧的“validated”标记不再有效。

## 本次实际验证

已修复测试框架中每个源文件单独持有失败计数的问题，失败现在汇总至主程序。
修复后单元测试重新通过。新增只读命令：

```sh
./build/doorfast --inspect-pcap capture.pcap IS:2-1-101-1
```

对原始 gvs-incoming-three-20260907.pcap 的本地检查结果：
`packets=21431 control=202 accepted=0 invalid=9 state=0`。
accepted 是当前观察器接受的来电帧数，不是完整通话次数；
invalid 混合了包解析与会话拒绝，尚不能作为网络损坏统计。
这个结果不证明设备无来电，也不证明主机模式成立。
当前仍缺少完整运行服务、在线维护、双向呼叫/媒体协议以及替代室内机的现场验证。

> 日期：2026-09-08
> 范围：已授权的 MT8157 APK 静态材料与离线 UDP/8300 会话抓包。本文不发送报文、不提取密钥，也不把尚未验证的字段用于控制设备。

Doorfast 当前将 `family=0x03, opcode=0x81` 保留为未知应答帧；静态发送链表明普通来电请求候选为 `0x01`。本文件后续保留早期分析结构以记录更正过程，但所有将 `0x81` 当作来电入口的结论均已撤回，不能作为实现或互操作依据。

## 范围与复现

本次分析范围在 `gvs-session-capture-20260907/scope.md` 中声明为获授权的本地 GVS 测试会话。厂商报告结构选择为 `flavor = null`：这是兼容性逆向记录，不是恶意样本或漏洞报告。

以下命令只读取文件；将 `MOOGREN_WORKDIR` 设为本机 `moogren/work` 目录后可复现。

```sh
/usr/sbin/tcpdump -nn -XX \
  -r "$MOOGREN_WORKDIR/gvs-session-capture-20260907/evidence/gvs-incoming-three-20260907.pcap" \
  'udp port 8300'

rg -n -C 12 '0x81' \
  "$MOOGREN_WORKDIR/../analysis/mt8157_apk_decoded/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
```

## 已观察到的控制帧

| 帧族/操作码 | 捕获中的方向与长度 | APK 静态证据 | 当前 Doorfast 处理 |
|---|---|---|---|
| `0x03/0x81` | 门口侧到室内侧，载荷 7 字节；重复约 1 秒 | 静态发送链标记为 `sendCallReply`；不能据此推出请求入口 | 保留为 `Unknown`；不启动会话 |
| `0x03/0x55` | 紧跟首个 `0x81`，载荷 3 字节；随后出现 UDP/8303 媒体流 | `GVS_Protocol.b([B,String,int,int)` 日志为 `sendVedioAsk`，固定写入 `0x55` | 保留为未知控制帧；不发送 |
| `0x03/0x51` | 双方均可见，零载荷 | `GVS_Protocol.e([B,String,int)` 日志为 `sendHandAsk`，固定写入 `0x51` | 保留为未知控制帧；不发送 |
| `0x03/0x52` | 与 `0x51` 相邻，零载荷 | `GVS_Protocol.f([B,String,int)` 日志为 `sendHandReply`，固定写入 `0x52` | 保留为未知控制帧；不发送 |
| `0x03/0x56` | 一字节载荷 `00` | 静态发送函数固定写入 `0x56`；名称未在该函数中保留 | 保留为未知控制帧；不发送 |
| `0x03/0x57` | 对 `0x56` 的反向一字节载荷响应 | `GVS_Protocol.d([B,String,int,int)` 日志为 `sendTimeCountSyncReply`，固定写入 `0x57` | 保留为未知控制帧；不发送 |

`0x03/0x50` 在另一份主动会话中存在，但静态发送函数日志为 `sendBusy`。因此它不应被视为“会话建立”依据；现有 Doorfast 的该映射只是待复核假设，后续需要单独修正和覆盖测试。

## 证据链

### E-001

- title: 入站三方会话中出现 `0x03/0x81`，并紧跟媒体请求
- observed_at: 2026-09-07 抓包时间窗
- source_type: network
- source_ref: `gvs-incoming-three-20260907.pcap`
- content_hash: `sha256:dfc712339604ea90427c4a900f40570acfbcf0ddbeafb5a40ca0cb9db3e7e367`
- artifact_path: 外部获授权工作材料；不复制进代码仓库
- repro_command: 见“范围与复现”中的 `tcpdump` 命令
- raw_excerpt: 公共头后为 `03 81 07 00`，随后出现 `03 55 03 00`；约一秒后开始 UDP/8303 流量
- linked_workitem: n/a
- supersedes: none

### E-002

- title: 已撤回：APK 待机分支把 `0x81` 送入来电路径
- observed_at: 2026-09-08
- source_type: file
- source_ref: `TalkBackBusiness.smali`
- content_hash: `sha256:e9dd4327e7dd378cc17324bb3fa6155c6f4e065c6f312fe24ccda573630c657c`
- artifact_path: 外部获授权分析材料；不复制进代码仓库
- repro_command: 见“范围与复现”中的 `rg` 命令
- raw_excerpt: 历史误读记录；后续调用链复核确认 `0x81` 为 `sendCallReply`，本条不再支持来电入口结论
- linked_workitem: n/a
- supersedes: none

### E-003

- title: APK 协议发送函数保留媒体、挂断与计时同步的符号名
- observed_at: 2026-09-08
- source_type: file
- source_ref: `com/gvs/general/protocol/c.smali`
- content_hash: `sha256:f00c40bf2e45511d6e55ed5903d2231877073d43a2d78f1485d306eeb8a20196`
- artifact_path: 外部获授权分析材料；不复制进代码仓库
- repro_command: `rg -n -C 8 'sendVedioAsk|sendHandAsk|sendHandReply|sendTimeCountSyncReply' "$MOOGREN_WORKDIR/../analysis/mt8157_apk_decoded/smali_classes3/com/gvs/general/protocol/c.smali"`
- raw_excerpt: `0x55`、`0x51`、`0x52`、`0x57` 分别由上述具名函数写入
- linked_workitem: n/a
- supersedes: none

### F-001

- title: 已撤回：`0x03/0x81` 是入站来电入口
- severity: n/a_re
- category: reverse_algo
- status: superseded
- evidence_ids: [E-001, E-002]
- location: `TalkBackBusiness.smali` 待机处理分支；`src/gvs_frame.c`
- impact: 该历史判断不得用于启动来电会话；当前实现以 `0x01` 为普通来电请求候选并保留 `0x81` 为未知。
- confidence: high（撤回结论）
- repro_steps:
  1. 用 E-001 命令读取抓包并定位 `03 81 07 00`。
  2. 用发送函数调用链复核 `0x81` 的 `sendCallReply` 语义。
  3. 运行 `make -B test`，验证 `0x81` 保持为 `Unknown`。
- remediation: n/a
- optional_attack:

### F-002

- title: 当前 `0x03/0x50` 的 “SessionEstablished” 命名缺乏支持
- severity: n/a_re
- category: other
- status: candidate
- evidence_ids: [E-003]
- location: `src/gvs_frame.c`
- impact: 如果将忙碌响应误作建链，Doorfast 可能把未建立的会话错误转入通话状态。
- confidence: medium
- repro_steps:
  1. 定位 `sendBusy` 对 `0x50` 的静态写入。
  2. 在主动会话抓包中核对该帧的上下文，再修改映射与测试。
- remediation: 后续仅在有静态处理分支和抓包时序两类证据后决定其事件类型。
- optional_attack:

### P-001

- title: 已撤回的 0x81 来电路径
- path_type: callflow
- start: UDP/8300 收到 GVS 公共头完整的 `0x03/0x81` 帧
- goal: 不建立来电会话；记录为历史误判
- steps:
  1. 解析 42 字节公共头并验证小端载荷长度。evidence: E-001 — finding: F-001
  2. 将 `0x03/0x81` 保留为 `Unknown`。evidence: 更正后的发送链 — finding: F-001
  3. 普通来电候选 `0x01` 仍须先比对已配置的本机逻辑地址，再启动铃响状态。
- residual_risks: `0x01` 的发送端互操作、媒体协商和在线维护仍未完成真实设备验证。

## 下一步

已移除未经证实的 `0x03/0x50 → SessionEstablished` 映射，并已实现 `IS:楼栋-单元-房间-分机` 到六字节 BCD 地址的纯本地解析、显式配置校验及前五字节筛选。离线回放管线现可将 Ethernet/IPv4/UDP 的 8300 端口载荷送入同一观察器，且只统计结果、不写入或发送网络数据。下一步才是在隔离环境中为 `0x55` 的媒体协商补充被动观测和测试。
