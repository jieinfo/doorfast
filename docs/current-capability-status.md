# Doorfast 当前能力与验证状态

更新日期：2026-09-15
代码基线：`origin/main` 的 `39d9c5d`，软件包 `0.1.0-r39`
HA 集成基线：`doorfastforha` 的 `081c4b3`（HA/VM 验收 runner 已合并）

本文按当前已合并代码记录能力，不以历史路线图或单次构建结果代替实现核对。

## 如何理解状态

| 层级 | 含义 |
|---|---|
| 已接入运行时 | 模块已由守护进程初始化，并进入真实捕获、发送或状态循环 |
| 项目测试通过 | 单元、回放、CLI、HTTP 或虚拟对端测试能复核项目行为 |
| PCAP 交叉验证 | 字段或状态转换能与厂商交付数据中的现场抓包对应 |
| 实体设备验收 | Doorfast 已在 MT8157 离线条件下与用户设备形成完整闭环 |

涉及开锁、电梯、独立上线和双向媒体时，前三层不能代替实体设备验收。协议回复只表示对端处理了请求，不能直接解释为门锁已打开或电梯已到达。

## 当前基线能力

| 能力 | 当前实现 | 已有证据 | 尚未证明 |
|---|---|---|---|
| 主机模式传输 | `active_host` 打开 UDP/8300，生成厂商公共头，学习已观察对端路由并发送控制、在线和同步报文 | 代码、单元测试、历史目标系统测试 | 门口机在 MT8157 离线后的冷启动识别和长期在线 |
| 来电 | 解析 `03/01`，建立带 generation 的会话，每次有效来电发送 `03/81` 回执 | 静态材料、PCAP、回放和运行时测试 | 真实门口机接受 Doorfast 回执 |
| 接听与挂断 | `03/03 → 03/83`、`03/02 → 03/82`，绑定会话 generation，包含有限重试、确认和超时 | PCAP、构帧测试、状态机测试 | 实体通话闭环、忙线、转接和跨固件行为 |
| 通话保活 | `03/51`/`03/52` 已接入 UDP 发送和运行时会话清理 | 静态材料、PCAP、测试 | 真机长时间通话和断网恢复 |
| 直接开锁 | 当前会话中发送 `04/09`，关联 `04/89`，每个 generation 最多提交一次 | 静态材料、PCAP 中的请求/结果、构帧和运行时测试 | 实体门锁动作；挑战路径 `04/11` 未实现 |
| 手动召梯 | ubus/HTTP 接受 `up` 或 `down`，发送 `08/02`，一秒后最多重试一次，两秒截止，关联 `08/82` | 静态材料、PCAP 请求样本、测试 | `08/82` 现场样本和实体电梯动作 |
| 自动召梯 | 接受来电后每个 generation 自动提交一次向上召梯 | 代码、测试 | 实体电梯动作 |
| 电梯状态 | 主机模式每秒发送 `08/03`，有界解析 `08/83` 并发布状态和年龄 | 静态材料、构帧和解析测试 | `08/83` 现场样本；任意楼层选择未实现 |
| 视频接收 | 接收 UDP/8303，校验媒体端点，重组不超过 1 MiB 的分片，校验 JPEG 并按会话缓存 | 静态材料、解析和重组测试 | 真实画面、丢片表现、延迟和长期资源使用 |
| 视频访问 | HTTP 提供 generation 绑定的 `latest.jpg`，支持 ETag、过期代次拒绝和发布竞态保护 | HTTP 和文件发布测试 | 连续视频流和浏览器端音视频同步 |
| 下行音频 | 接收 UDP/8302，统计丢包、重复和迟到帧，输出 generation 绑定的增量 WAV 块并保留最近四块 | 静态材料、解析、缓冲和 HTTP 测试 | 真机音频格式、实际播放质量和延迟 |
| 上行音频核心 | 通话中接收本机 PCM 数据报，以 8 kHz、单声道、160 采样编码为 G.711 A-law 并发送 UDP/8302 | 编码、节奏、代次、socket、打包工具、HTTP 批次和发送测试；HA 按住说话卡片已实现浏览器采集与重采样；隔离 GVS 对端 harness 已覆盖来电、接听确认、03/51 保活和音频帧采集 | 回声消除效果和真机收听；VM 环回只证明 Doorfast 生产发送链路，不证明实体扬声器可懂度 |
| HTTP PCM 生产者 | 三个 CGI 路由用 runtime、generation、单生产者 token、连续 sequence 和两秒租约接收每批 1–5 个 PCM 帧；失败保留已接收前缀供客户端恢复 | C/CLI/CGI 测试、Actions r39 交叉编译、ImmortalWrt VM 安装拒绝与隔离接收验收；HA/VM 验收 runner 的 42 项真实 HA fixture 检查 | HA/浏览器端在实体通话中的实际采集与重采样；实体扬声器可懂度、延迟、回声及长通话稳定性 |
| 本地控制接口 | ubus 和 HTTP 提供 `status`、`answer`、`hangup`、`unlock`、`call_elevator` | ubus 和 HTTP 测试 | HTTP CGI 自身没有独立令牌校验，部署端必须限制访问 |
| 本地事件流 | `/var/run/doorfast/events.sock` 发布来电、通话建立、挂断、超时和抢占 JSON Lines；事件绑定 generation，每客户端队列上限 64，慢客户端断开 | C 测试、Actions、VM socket 属主和权限检查 | 实体门口机触发的连续事件序列 |
| HA 主动事件 | 独立 relay 从本地 socket 读取事件，经 CA 和主机名校验的 HTTPS、Bearer token 投递到 HA；HA 刷新权威状态后去重和派发，五秒轮询兜底 | HA 35 项测试、Actions 交叉编译、VM TLS 投递和断线重试；真实 HA 2024.11.0 容器中 42 项 fixture 验收 | 实体门口机触发的连续事件、长期断网和高频来电运行 |
| 管理与部署 | procd、UCI、只读 LuCI 状态页、部署预检查、证据记录器、站点清单、事件 relay 和 OpenWrt 用户组生命周期 | CLI、配置、记录器、LuCI、Actions 和 ImmortalWrt VM 测试 | LuCI relay 配置表单、自动升级和完整发布流程 |

## 当前对外使用边界

主程序通过本地 Unix socket 主动发布呼叫事件。可选 `doorfast-event-relay` 使用 HTTPS 将事件送到 `doorfastforha` 的认证入口；HA 收到事件后仍先读取 `/api/v1/status`，因此事件只负责降低发现延迟，状态接口仍是权威来源。MQTT 和手机通知不在当前实现范围。

视频接口提供最新 JPEG 画面，不是 HLS、RTSP 或 WebRTC 流。下行音频接口能提供增量 WAV 块；`doorfast-pcm-submit` 提供本机单帧入口，r39 的 HTTP PCM 接口提供带租约、序号和失败恢复的网络生产者入口。`doorfastforha` 现已提供经过 HA 身份验证 WebSocket 绑定的按住说话卡片、浏览器麦克风采集和 8 kHz 重采样；真实 HA fixture 已验证其生命周期和 PCM 契约，但尚未在实体 MT8157 通话中验证可懂度、延迟和回声。

HTTP PCM 的 token 只协调一个生产者，不能代替认证或加密。部署时必须把 CGI 限制在可信 HA/路由网络，或放在经过认证的 HTTPS 后面。HTTP 成功只证明本地 Unix ingress 已接收帧，不代表门口机已经收到或播放音频。

LuCI 当前只显示状态。接口选择、逻辑身份、主机模式、开锁材料和自动召梯仍需编辑 UCI；接听、挂断、开锁和召梯需通过 ubus、HTTP 或 Home Assistant 调用。

## 已知实现问题

1. 自动召梯运行路径固定传入 `up`；旧配置中的 `call_elev_direction` 会被兼容性忽略。当前用户要求的来电自动向上不受影响，向下召梯仍需另行设计和验证。
2. relay 当前仅接受 HTTPS authority 配置，目标路径固定为 `/api/doorfast/<entry_id>`；token 必须位于 root 所有的 0600 文件中。
3. HTTP CGI 没有独立认证逻辑；包括 PCM 生产者在内的访问控制属于部署前置条件。PCM session token 不是通用 HTTP 身份凭据。

## 本轮 Slice 4 验收证据

`doorfastforha` 的 `doorfast_ha_e2e` 是测试工具，不会进入 Doorfast 或 HA 的运行时安装包。它在同一 HA 容器内运行，避免 Docker/Colima 的主机 loopback 路由假设；2026-09-15 使用 HA `2024.11.0` 和 `doorfast` `a5fcca3` 完成 42 项检查。检查包括 REST config-flow 建项、四个静态前端资源、WebSocket start/submit/stop、断线与 generation 清理、双 entry 隔离、挂断清理以及 disable/re-enable unload/reload。JSONL 证据只保留布尔结果和脱敏摘要，不保存 token、PCM 或原始采集标识。该证据验证 HA 集成和网络契约，不等同于真实门口机音频验收。

ImmortalWrt `25.12.1` VM 的主机模式测试使用隔离 GVS 对端 harness 注入合成来电，并观察 Doorfast 生产 UDP 控制和媒体帧。对端只绑定 loopback，发送 ACK 后回复 `03/51` 保活；证据可用于确认 Doorfast 的来电→接听确认→保活→PCM/UDP 发包时序，脱敏记录见 [`docs/evidence/2026-09-15-vm-gvs-talking.json`](evidence/2026-09-15-vm-gvs-talking.json)。本次 VM 为等待守护进程完成 talking 状态使用了 5 秒本地 settle 值，这不是厂商时序常量。它不证明 MT8157 实体设备接受回执、门锁/电梯动作、视频画面或扬声器可懂度。

## 当前验证结果

当前 C 测试、主程序编译、CLI、HTTP、PCM 隔离验收、软件包清单、记录器、站点清单和 LuCI JavaScript 测试通过；`doorfastforha` 主线已包含 HA/VM 验收 runner 及其 5 项 runner 单元测试。GitHub Actions [run 34943868811](https://github.com/jieinfo/doorfast/actions/runs/34943868811) 构建的是 PR head `958d93a5`；其相同代码树随后以合并提交 `5cd91ef` 进入主分支。该运行完成了 x86_64 ImmortalWrt 25.12.1 的 r39 交叉编译。下载产物的 SHA-256 为：

| 产物 | SHA-256 |
|---|---|
| `doorfast-pcm-http-acceptance` | `027cfdbcdaafca65e52f4e0f5401929f80b5f2e8fd714cf513dc2c79baf828ad` |
| `doorfast.apk` | `194e7f966b096a62581f11e36637a9d028f3e4883023c5868c42f48bc6241e4b` |
| `luci-app-doorfast.apk` | `4846db90a193a2ec9bab91e827fb3c67cdc4db6902abbcd479939f2ab0932262` |

`gvs-incoming-three-20260907.pcap` 原始抓包缺少初始 `03/01`。只读分析确认其中出现 `03/02`、`03/03`、`03/51`、`03/52`、`03/55`、`03/56`、`03/57`、`03/81`、`03/82`、`03/83`、`04/09` 和 `04/89`。补入六个明确标为合成的初始请求后，当前回放器得到六次来电、四次进入通话、四次挂断和两次超时。该实验验证状态机与已捕获后续报文相容，不证明合成请求就是丢失报文，也不证明实体设备互操作。

本次在 `127.0.0.1:2222` 的 ImmortalWrt 25.12.1 VM 使用 Actions 产出的 r38 APK 完成 r37→r38 升级。升级前移走手工目录后，软件包正确创建空的 `/etc/doorfast`，属主为 `root:root`、权限为 `0750`；APK 的 `.rusers` 为 `:doorfast`，`/var/run/doorfast` 为 `root:doorfast 0750`，事件 socket 为 `root:doorfast 0660`。合并后的 VM 验收脚本也已通过：一次性测试 CA 下，relay 校验 CA 和主机名，携带 Bearer token 投递 generation 绑定的准确 JSON；接收端首次断开后，同一请求体按退避重试并成功送达；token 权限为 `0600` 时可用、放宽为 `0640` 时被拒绝；测试前后网络 UCI 和防火墙结构未变化。

HTTP PCM 本次在 Linux `6.12.94`、ImmortalWrt `25.12.1`、`x86_64` 的隔离 VM 上用上述 Actions 产物完成 r38→r39 升级。实际安装的 CGI 在空闲状态完成过期 generation、错误方法、短正文和超限正文的入口拒绝与无状态变化检查；由于空闲状态检查先于正文校验，这里的短正文和超限正文均返回 `409`，不构成已安装 helper 的正文校验证据。非安装的验收程序在私有临时目录验证了 talking 状态下的正文长度、短正文和尾随字节拒绝，并完成单生产者排他、释放与超时接管、五帧顺序和精确 `DFPCM01` 内容、部分失败恢复、锁等待后的旧请求拒绝以及重复序号不重放。四个实测帧间隔为 `20.112104`、`21.093111`、`21.360112`、`21.619112` ms。验收前后 Doorfast PID、服务、调用/音频状态、网络 UCI、防火墙和 nftables 保持一致，生产状态与临时上传目录均完成清理。

VM 验收脚本依赖 Python 3；本次只在隔离 VM 中为运行验收安装 Python 3，它不是 `doorfast` APK 的运行依赖。上述结果证明 r39 包安装、拒绝路径、本地 Unix 数据报接收、节奏、恢复和清理行为，不包含实体 MT8157 播放测试，也不证明扬声器格式、可懂度、端到端延迟、回声或长通话稳定性。

## 下一步顺序

1. 补齐 LuCI 的主机模式、接口、逻辑身份、门禁材料与 relay 配置入口，并为接听、挂断、开锁和手动召梯提供受 ACL 约束的操作界面。
2. 在 MT8157 离线的受控现场依次验收独立上线、来电回执、挂断、开锁、召梯、视频和双向音频。
3. 使用现场 PCAP 回填仍缺失的 `08/82`、`08/83`、视频丢片和真实双向音频证据，不把厂商静态材料单独作为结论。
4. 完成 HA 长期断网、高频来电、浏览器回声与实体音频质量验证，再整理可发布的软件包升级流程。
