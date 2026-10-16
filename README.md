# Doorfast

Doorfast 是面向 x86_64 ImmortalWrt 25.12.1 的 GVS 可视门禁适配服务。它提供被动观察、主机模式呼叫与控制、双向音频、视频接收、主动预览、Home Assistant 事件接入，以及现场部署取证工具。

> **文档规则：本 README 是项目能力、配置、接口、验证状态和后续工作的唯一权威文档。** 仓库不再维护并行的计划、设计或进度 Markdown。`docs/` 中可以保留带明确标识的抓包、逆向、来源和验收证据记录，但其中的历史状态与下一步不构成当前项目结论。功能变化必须同时更新本文件；若文档与代码冲突，以已合并代码和可复现测试为准。

> **现场边界：协议报文已成功提交或收到协议确认，不等于门锁、电梯、门口机或物业平台已实际执行。** 本文分别记录代码接线、测试、材料交叉验证和实体设备闭环，禁止用前三者代替实机结论。

> **连续音频 v1 验收边界（2026-10-12）：** 接收重排、有限补偿、增量 PCM WAV 发布与 HTTP 身份隔离已由本地合成测试覆盖。Home Assistant 连续播放/采集接线、目标设备扬声器听音、回声与实际时延、HTTPS 部署验收仍未完成；测试没有证明实机时延低于任何目标值。

本文基于 2026-09-18 对完整代码和测试的复核，覆盖 `src/`、3 个 APK 配方、LuCI、init 脚本、CLI、HTTP/ubus/Unix 接口和 CI。此前“主程序只构造并排队控制报文”的描述已经过时：呼叫、在线维护、开锁、召梯、音频和视频主链路都已接入生产 UDP；尚未完成的实体设备验证见“已知限制和未完成项”。

## 安全和部署前提

- 软件包和主机模式默认关闭。首次安装不会猜测网口、逻辑身份、IP、子网掩码或门禁材料。
- 被动模式只抓包，不给门禁网口绑定室内机 IP；主机模式会临时给指定网口添加室内机 IP，并主动发送 GVS UDP。
- 透明串联桥由管理员提前配置。Doorfast 只检查和观察，不创建桥、不修改路由、防火墙、DHCP 或 RA。
- CGI 控制桥自身不实现 Bearer Token 鉴权，应只允许受信管理网或经 Home Assistant 代理访问，禁止直接暴露到 WAN。
- HTTP 语音只用于可信内网；公网语音建议通过 HTTPS 代理访问，不建议公开暴露 Doorfast CGI。连续音频 v1 不新增认证或证书部署能力。
- 厂商材料和 PCAP 是实现依据之一，但可能不完整或不准确。任何主动能力都必须以现场双向抓包和实体结果继续验证。
- 现场串联可能中断原 MT8157。部署时必须保留物理直连回退方案，并先运行只读预检。

## 证据等级

本文的能力矩阵使用以下等级：

| 等级 | 含义 | 能证明什么 |
|---|---|---|
| L1 生产接线 | 功能从主守护进程连接到真实 socket、文件、进程或系统接口 | 运行时会执行该代码路径 |
| L2 测试验证 | 单元、CLI、HTTP、回放或 ImmortalWrt VM 验证通过 | 实现与测试模型一致 |
| L3 材料交叉验证 | 厂商资料、反编译结果或现场 PCAP 支持字段和方向 | 协议推断有外部证据 |
| L4 实机闭环 | 实体门口机、门锁、电梯或物业系统完成端到端动作 | 现场兼容性成立 |

矩阵中的“部分”表示只覆盖了该等级的一部分，不可提升为更高等级。

## 系统结构

```mermaid
flowchart LR
    station["门口机 / 物业 GVS 网络"]
    daemon["doorfast 守护进程"]
    ubus["ubus 与 CGI API"]
    event["Unix 事件流"]
    relay["事件 relay"]
    media["doorfast-media"]
    ffmpeg["FFmpeg H.264 编码"]
    go2rtc["Home Assistant go2rtc"]
    ha["doorfastforha"]
    luci["LuCI"]

    station <-- "UDP 8300 控制" --> daemon
    station <-- "UDP 8302 G.711 A-law" --> daemon
    station -- "UDP 8303 JPEG 分片" --> daemon
    daemon --> ubus
    daemon --> event --> relay --> ha
    luci --> ubus
    ubus --> ha
    daemon --> media --> ffmpeg --> go2rtc --> ha
```

单次守护进程运行期间以 `generation` 隔离呼叫、控制和媒体。新来电、抢占、挂断或超时会使旧 generation 的操作失效，避免把旧按钮、旧音频或旧画面提交到新会话。守护进程重启后 generation 会从 1 重新计数；接听、挂断、开锁和召梯同时强制匹配本次运行的 `runtime_id`，防止延迟请求碰撞到重启后的同号 generation。

## 软件包

| APK | 当前版本 | 内容 | 架构 |
|---|---|---|---|
| `doorfast` | `0.1.0-r54` | 守护进程、记录器、事件 relay、PCM HTTP/CLI、CGI 桥、init/UCI、站点清单工具 | x86_64 |
| `doorfast-media` | `0.1.0-r54` | 可选的 ABI v3 `/usr/lib/doorfast/media-v3.so`，由主守护进程 `dlopen()`；不安装独立服务 | x86_64 |
| `luci-app-doorfast` | `0.1.0-r17` | 状态、媒体、站点、自动化、部署、HA relay 和日志页面 | all |

`doorfast-media` 中的 `.so` 是本项目为 ImmortalWrt 编译的原生共享模块，不是 Android APK 内提取的二进制。

GitHub Actions 使用校验过 SHA-256 的官方 ImmortalWrt 25.12.1 x86_64 SDK，依次运行主机测试并构建这 3 个 APK。CI 产物还包含仅用于验收的 `doorfast-pcm-http-acceptance`，该文件不会安装到路由器。

## 当前能力矩阵

| 能力 | 当前实现 | L1 | L2 | L3 | L4 |
|---|---|---:|---:|---:|---:|
| 被动抓包与 VLAN-aware UDP 提取 | 指定物理口、VLAN、bridge 或 bond；失效后有限重试 | 是 | 是 | 是 | 部分 |
| GVS 身份与寻址 | 校验 `IS:楼栋-单元-房间-分机`，推导单播 IP、组播组和室内机 peer | 是 | 是 | 是 | 否 |
| 主机网口生命周期 | 独立主机网口、临时地址添加/删除、UDP 源地址绑定、UDP/8300 组播加入 | 是 | 是 | 是 | 否 |
| 来电 | 解析 `03/01`，建立 generation，发送 `03/81` 回执，处理抢占和超时；当前现场已验证来电送达和 `03/81` 回执，门口机不再显示“无应答” | 是 | 是 | 是 | 部分 |
| 接听 | 发送 `03/03`，跟踪重试和 `03/83` 确认 | 是 | 是 | 是 | 否 |
| 挂断 | 发送 `03/02`，跟踪重试和 `03/82` 确认 | 是 | 是 | 是 | 否 |
| 通话保活 | 发送和接收 `03/51`、`03/52`，维护通话状态 | 是 | 是 | 是 | 否 |
| 直接开锁 | 使用 8 字节门禁材料发送 `04/09`，解析 `04/89` | 是 | 是 | 是 | 否 |
| 手动召梯 | 向上/向下发送 `08/02`，解析 `08/82` | 是 | 是 | 部分 | 否 |
| 自动召梯 | 每个来电 generation 最多自动向上召梯一次；LuCI 可开关，默认关闭 | 是 | 是 | 部分 | 否 |
| 电梯状态 | 周期发送 `08/03`，解析 `08/83` 和轿厢状态 | 是 | 是 | 部分 | 否 |
| 下行音频 | 接收 UDP/8302 G.711 A-law，四槽重排、有限补偿、20 块增量 PCM WAV；v1 读取匹配 runtime/station/generation | 是 | 是 | 是（codec/报文字段） | 否 |
| 上行音频 | generation 绑定的 PCM 经 A-law 编码，以 8 kHz、160 样本/20 ms 发送 UDP/8302 | 是 | 是 | 是 | 否 |
| 通话视频 | 接收 UDP/8303 JPEG 分片，重组、校验、识别尺寸并发布最新帧 | 是 | 是 | 是 | 否 |
| 主动预览 | 发送 `03/04`，处理 `03/84`/`03/50`，用 `03/02` 停止 | 是 | 是 | 部分 | 否 |
| H.264/WebRTC 前置链路 | JPEG 经 FFmpeg 的 libx264/VAAPI/QSV 发布到预创建的 go2rtc RTSP 流 | 是 | 是 | 不适用 | 不适用 |
| 本地事件流 | `/var/run/doorfast/events.sock` 发布带 event ID 和 generation 的 JSONL | 是 | 是 | 不适用 | 不适用 |
| HA 事件 relay | HTTP/HTTPS、队列、重连、退避、令牌文件和轮询兜底 | 是 | 是 | 不适用 | 不适用 |
| LuCI | 状态/媒体、来电自动化、部署配置、HA relay、内存日志 | 是 | 是 | 不适用 | 不适用 |
| 透明串联取证 | 只读预检、轮转 PCAP、元数据、空间保护、前后站点清单比对 | 是 | 是 | 不适用 | 不适用 |
| 在线 peer 回复 | 收到 `07/01` 后，经事务队列和运行时厂商头发送真实 `07/81` UDP | 是 | 是 | 是 | 否 |
| 周期同步 | 维护者把已配置的厂商同步键按最多 20 项分片发送真实 `91/03` UDP；日志记录实际包数 | 是 | 是 | 是 | 否 |

### 已经接入真实 UDP 的控制路径

下列路径不是只停留在序列化器或内存队列：

- 来电回执 `03/81`。
- 接听 `03/03`、接听确认 `03/83`。
- 挂断 `03/02`、挂断确认 `03/82`。
- 通话握手/保活 `03/51` 和 `03/52`。
- 直接开锁 `04/09`、协议结果 `04/89`。
- 手动和自动召梯 `08/02`、协议确认 `08/82`。
- 电梯状态查询 `08/03`、状态响应 `08/83`。
- G.711 A-law 上行音频 UDP/8302。
- 主动预览请求 `03/04` 和停止 `03/02`。
- 在线 peer 回复 `07/81`。
- 维护者周期同步 `91/03`；只有已填写现场确认值的同步键会进入报文。

所有控制帧使用运行时厂商随机字段/变换字段。普通呼叫、在线回复、开锁和召梯控制优先使用最近 60 秒内观察到的 peer IPv4；表项在 60 秒边界失效，未命中时根据目标逻辑身份推导 IPv4，新报文会刷新路由。主动预览使用独立的 station 路由表：抓包学习的地址同样只在 60 秒内有效，也可使用明确配置的门口机 IP 作为回退。`status` 中的 `physical_result_confirmed` 对开锁和召梯保持为 `false`，因为协议成功不能证明机械动作。

## 安装与启动

将同一次 CI 或 Release 生成的 APK 安装到 ImmortalWrt 25.12.1 x86_64：

```sh
apk add --allow-untrusted ./doorfast.apk ./luci-app-doorfast.apk
# 需要主动 H.264 预览时再安装：
apk add --allow-untrusted ./doorfast-media.apk
```

安装后服务仍保持关闭。通过 LuCI 的“服务 → Doorfast → 部署配置”设置模式和网口，或编辑 `/etc/config/doorfast`，然后执行：

```sh
/etc/init.d/doorfast enable
/etc/init.d/doorfast restart
ubus call doorfast status
```

修改 `doorfast` 或 `doorfast-automation` 后，init reload 会停止旧实例、清理临时主机地址并按新配置启动。

## 配置被动观察

被动模式适合保留原 MT8157，并从镜像口或透明串联接口观察。门禁接口可以继续保持 OpenWrt 的“无协议”状态；Doorfast 不为其绑定室内机 IP。

```uci
config gvs 'main'
	option enabled '1'
	option passive_interface 'eth2'
	option host_interface ''
	option gvs_local_address 'IS:2-1-101-1'
	option passive_only '1'
	option active_host '0'
	option capture_promiscuous '0'
	option sync_state_path '/etc/config/doorfast-sync'
```

`gvs_local_address` 仍是必填项，用于只接纳发给本室内机的帧。`capture_promiscuous` 仅在交换网络和抓包拓扑确实需要时开启。

## 配置主机模式

主机模式用于 Doorfast 取代原 MT8157 的网络身份。它与被动模式使用独立的 `host_interface`，不会沿用被动网口选择。

```uci
config gvs 'main'
	option enabled '1'
	option passive_interface ''
	option host_interface 'eth2'
	option gvs_local_address 'IS:2-1-101-1'
	option indoor_ipaddr ''
	option indoor_netmask '255.255.255.0'
	option uplink_interface 'br-lan'
	option passive_only '0'
	option active_host '1'
	option capture_promiscuous '0'
	option multicast_mode 'auto'
	option multicast_address ''
	option access_material ''
	option sync_mini1_secretkey ''
	option sync_mini2_secretkey ''
	option sync_state_path '/etc/config/doorfast-sync'
```

主机模式规则：

- `gvs_local_address` 必须匹配 `IS:楼栋-单元-房间-分机`。楼栋为 1–99、单元为 1–9、楼层为 1–63、房号为 1–32、分机为 1–4，例如 `IS:2-1-101-1`。
- `indoor_ipaddr` 留空时按 GVS 身份推导；手动填写时覆盖推导值。当前实现不会等待物业系统通过专用配置协议下发 IP。
- `indoor_netmask` 必须按现场原室内机填写。现有材料不足以可靠推导通用掩码，因此不会自动猜测。
- init 脚本把 `IP/掩码` 临时添加到 `host_interface`，守护进程将 UDP sender 绑定到该 IP；停服时删除临时地址。
- `multicast_mode=auto` 按 GVS 身份推导组播地址；`multicast_mode=custom` 只接受 `224.0.0.0/4` 内的明确地址。两种模式都使用 UDP/8300，runner 会同时记录 derived/effective 值。
- `access_material` 是直接开锁报文使用的 8 字节材料，以 16 个十六进制字符填写。留空时开锁控制不可用，不生成默认值。
- `sync_mini1_secretkey` 和 `sync_mini2_secretkey` 是厂商材料确认的两个 Mini 同步键。它们属于站点敏感值，只能填写从原设备或物业配置获得的真实值；留空时不生成替代值，也不会把该键放入周期 `91/03`。
- `uplink_interface` 仅标识可选的管理/上行接口，不改变门禁网口。

当前现场已经完成门口机来电送达与 `03/81` 回执验证，门口机不再显示“无应答”，因此早期故障不再是未解决项。`07/81` peer reply 和 `91/03` 周期同步已经接入生产发送，但新发送路径仍需在现场做冷启动、长时间在线和双向 PCAP 回归；代码接线不能代替物业系统认可的 L4 证据。

## 主动门口机发现

主动发现只在主机模式启用时运行。每次 `station_scan` 触发一个固定的三帧 `07/06` 扫描序列；运行时只接纳目标类型和校验通过的匹配 `07/86` 回复，并在候选缓存中记录逻辑地址、IPv4、首次/最近观察时间和回复次数。候选只用于 LuCI 的人工采用流程，不会自动写入配置。

可用接口如下：

- `ubus call doorfast station_scan '{}'`：安排一次扫描，返回当前 `runtime_id` 和 `scheduled`，不表示已经发现实体设备。
- `ubus call doorfast station_candidates '{}'`：读取候选缓存，候选不会直接出现在 HA 的已配置站点列表中。
- `ubus call doorfast stations '{}'`：读取已配置门口机、`revision`、路由来源和 `route_fresh`；`/api/v1/stations` 只暴露这些已配置条目。

普通 peer 和门口机路由的观察新鲜度为 60 秒。超时条目会标记为失效并阻止主动媒体使用，重新观察或明确配置的门口机地址后才能恢复。扫描、候选读取和站点列表不会修改网络 UCI；`runtime_id` 变化时客户端必须丢弃旧的候选和站点状态。

Task 7 的软件验收可用仓库 fixture 复现：

```sh
python3 -B tests/run_doorfast_vm_stations.py \
  tests/fixtures/fake-vm-preflight-ssh.sh
```

该 runner 只证明一次扫描生命周期的三帧发送结果（fixture 通过 `frames_sent=3` 暴露）、候选与已配置站点数量、ubus 与 `/api/v1/stations` 投影一致、revision、路由新鲜度和组播推导值；输出中的 `physical_registration` 与 `physical_actions` 固定为 `unconfirmed`，不代表物业系统已经注册，也不代表门口机、门锁或电梯已经动作。

## 来电、控制和站点解锁

`ubus call doorfast status` 是运行状态的权威入口。来电创建 generation；接听和挂断仍要求提交当前 generation 以及状态中的 `runtime_id`。开锁按配置的 `station_id` 定位目标室外机，不依赖当前通话、generation 或 `runtime_id`；召梯操作由服务生成独立 transaction ID。Doorfast 重启后，客户端仍必须重新读取状态并丢弃旧 runtime 下的 generation 和事件高水位。

常用 ubus 方法：

| 方法 | 参数 | 返回语义 |
|---|---|---|
| `status` | `{}` | 同步、呼叫、开锁、电梯、音频、视频和媒体状态 |
| `logs` | `{}` | 最多 128 条内存日志；重启后清空 |
| `answer` | `runtime_id`、`generation`、`primary_media_port`、`secondary_media_port`、`duration_seconds` | `queued=true` 只表示已进入发送事务 |
| `hangup` | `runtime_id`、`generation`、`reason` | `queued=true` 只表示已进入发送事务 |
| `unlock` | `station_id` | `submitted=true` 只表示指定室外机的协议事务已提交 |
| `call_elevator` | `runtime_id`、`direction=up|down` | 返回 transaction ID，不宣称电梯已动作 |
| `monitor_start` | `{}` | 返回新的预览 generation 和 `queued` 状态 |
| `monitor_stop` | `generation` | 只停止匹配的预览 |
| `monitor_viewer` | `generation`、`active` | 更新当前 generation 的观看者状态 |
| `monitor_status` | `{}` | 返回媒体模块状态 |
| `media_credentials` | 密码/令牌写入或清除字段 | 保存 root-only 凭据，不回显明文 |

示例：

```sh
status="$(ubus call doorfast status)"
generation="$(printf '%s' "$status" | jsonfilter -e '@.call.generation')"
runtime_id="$(printf '%s' "$status" | jsonfilter -e '@.runtime_id')"

ubus call doorfast answer \
  "{\"runtime_id\":\"$runtime_id\",\"generation\":$generation,\"primary_media_port\":8303,\"secondary_media_port\":8302,\"duration_seconds\":60}"
ubus call doorfast unlock \
  '{"station_id":"gate_main"}'
ubus call doorfast call_elevator \
  "{\"runtime_id\":\"$runtime_id\",\"direction\":\"up\"}"
ubus call doorfast hangup \
  "{\"runtime_id\":\"$runtime_id\",\"generation\":$generation,\"reason\":0}"
```

来电自动化位于 LuCI 的“来电自动化”页，配置保存在 `/etc/config/doorfast-automation`。默认关闭；“来电自动向上召梯”按每个来电 generation 最多提交一次，“来电自动解锁”按来电来源的逻辑地址只提交对应室外机的开锁事务，不会解锁其他站点。

`status.access.station_id` 是最近一次**协议解锁事务**的目标：通过事务保存的目的逻辑地址匹配当前启用站点，而非通过当前来电或当前预览推测。目标已移除、被禁用或匹配不唯一时省略该字段；会话结束后仍可保留最近回复的归属。`manual_state=waiting_media|waiting_reply` 和 `manual_station_id` 另行表示正在处理的手动解锁；准备期间，原 `access.state/generation/raw_status` 可能仍属于上次事务，不能当成本次结果。无活动会话的手动解锁最多准备 30 秒，满足下述当前确认或真实媒体条件后才发送；准备取消或超时且没有发送时，不产生新的协议回复。`protocol_completed` 只表示门口机协议确认，仍不证明门已经打开。

## 按站点解锁与手动解锁准备（2026-10-16 本地实现）

已实现按站点凭据、轻量准备、分段计时和 LuCI/HA 展示，并完成离线验证；尚未进行本轮目标 SDK 编译、路由器部署或实机延迟比较。现有现场已确认的来电自动解锁及匹配回复后挂断行为继续保留。不能据此声称已经缩短实机等待、物理门已打开或每次都能成功。

### 手动请求与兼容路径

请求仍只接受站点 ID：

```json
{"station_id":"gate_main"}
```

`POST /api/v1/unlock` 转发到 `ubus call doorfast unlock`。受理响应包含 `submitted:true`、`station_id`、`runtime_id`、16 位小写十六进制不透明字符串 `request_id` 和 `phase`。受理可能立即进入失败终态；`submitted` 不表示发包或协议成功。同站点待处理请求复用身份，不重复发包；不同站点不能抢占当前开锁事务。终态后下一次明确点击才创建新请求。

| 媒体能力/当前会话 | 手动开锁准备条件 | `preparation_path` |
| --- | --- | --- |
| 可选 `df_media_module_unlock_prepare_api_v1` 可用，无可复用会话 | 本次独占轻量准备收到当前 `03/04` 窗口内、来源/目的/实际 IP/正文均校验的 `03/84` 后快发；未取得有效确认时，保留当前完整 JPEG 的兼容路径。不要求编码器或 RTSP 凭据 | `lightweight` |
| 同站点已有有效媒体会话 | 复用当前有效真实媒体；不重建或接管其他站点 | `reused` |
| 旧模块没有可选准备能力 | 保留完整媒体 `ready`、`source_live` 与 publishing/viewing 条件 | `full_media` |
| 能力存在但 ABI 无效、启动/状态读取失败 | 明确失败；不绕过到兼容路径 | 不产生伪造就绪 |

固定媒体 ABI v3 和可选准备结构布局未变；`ready`/`current_attempt` 仍表示经过校验的当前 JPEG，快发单独检查有效确认，不伪造媒体就绪。历史/窗口外 ACK、停止、过期、忙重试、冷却或被观看者/通话接管的准备不能授权快发；已有会话和旧模块仍走真实媒体条件。轻量准备沿用原预览信令调度、忙重试、来电优先和冷却。准备截止仍为 30 秒，满足条件后在服务调度发送一次 `04/09`，发送后独立保留 1 秒回复窗口；发送失败、拒绝或超时后均不自动等待 JPEG 再发第二次，也不尝试挑战开锁。

2026-10-16 单次授权的一单元试验在首帧前发送普通开锁，确认至发送约 23 ms、发送至协议回复约 30 ms，比该次原生首帧早约 689 ms；没有现场门状态确认。该次仍先经历约 8.1 秒准备（含忙回复），快发不消除此等待；尚不能推广为所有门口机的成功率或量产延迟保证。正式代码本轮仅做本地验证，未部署。

清理只针对本次拥有的站点和媒体 generation；失败保留有界 `cleanup_error` 并重试清理，阻止新事务替换。观看者或来电接管后，解锁清理不会停止新所有者；离线测试已验证接管后正常编码器管线收到媒体输入，不代表真实 RTSP 服务收到发布。来电自动解锁继续使用原 CALL-purpose、ready 和配置延迟条件（不新增手动路径的 source_live 条件），同一站点选材；匹配成功或拒绝回复后按原规则挂断对应来电。

### LuCI：每台门口机的方式与材料

| 站点 UCI `unlock_mode` | 材料来源 | 缺失/无效时 |
| --- | --- | --- |
| 未配置或 `inherit` | 现有全局 `access_material` | 不可解锁；不自动生成材料 |
| `direct` | 本站点独立的 8 字节材料，输入为 16 个十六进制字符 | 不可解锁；不回退全局 |
| `disabled` | 无 | 禁止本站点手动和自动解锁 |

UCI 模式是唯一生效方式权威，旧私有记录不能覆盖 disabled/inherit。普通 `04/09 → 04/89` 帧、声明长度兼容和一次会话一次尝试规则不变；材料不是普通门禁 PIN，不猜密钥或挪用同步 Mini 密钥。随机挑战/加密分支仍只有静态材料，没有动态验收，未实现为可用方式；[厂商材料基线](docs/gvs-portable-package-baseline.md)的协议能力矩阵未因此改变。

独立材料保存在 root 持有、0600 的 `/etc/doorfast/access-credentials`，受控路径、原子替换；不写普通站点 UCI、公开列表、状态、日志或 HA。现有全局材料不自动迁移或删除。损坏/不可读私有存储以 `store_unavailable` 报告不可用；daemon 源码不因该读取错误单独退出，实际 daemon 启动边界尚未在目标系统验证。

LuCI 输入只写不读：留空保留，显式清除只删本站点记录，不改变 UCI 模式。管理身份认证与受限 ACL 允许 `access_credentials_status(station_id)`、`access_credentials_set(station_id, material, mode=direct)` 和 `access_credentials_clear(station_id)`；普通 HTTP CGI 不开放凭据管理。status 只返回有效方式、可用性、原因及 `material_configured` 布尔值，不返回保存材料。set 的 direct 描述待存私有记录，不能覆盖 live UCI 方式。

新站点必须先保存并应用，再填写独立材料；未知新行带材料或清除操作会在任何写入前失败。批量保存先完整校验，再写私有记录和 UCI，等待原生 `uci-applied` 事件及运行态元数据确认；材料写失败、应用未确认或运行态未生效不会提示成功，也不自动第二次重启。写/清除前取消受影响的未发送准备；清理失败则文件字节不变，其他站点准备不受影响。已发送事务保留原归属。站点 ID 不应复用为另一门口机。

### 状态、耗时与 HA

`status.access.version=1` 新增 `stations[]` 有效方式/来源/可用性，以及每个配置站点最多一条最新受理记录的 `requests[]`；`request` 是当前/最近记录的便捷投影，未受理时省略，`requests=[]`。旧 `configured/state/generation/raw_status/manual_state/manual_station_id` 保留；顶层 configured 仍是全局材料状态，不能替代站点有效状态。`manual_state` 仅 idle/waiting_media/waiting_reply，终态见新记录。

| 字段 | 值或语义 |
| --- | --- |
| `phase` | waiting_media、waiting_reply、protocol_completed、protocol_rejected、preparation_timeout、expired（回复超时）、cancelled、send_failed |
| `wait_reason` | none、busy、cooldown、waiting_media、compatibility、station_unavailable、unknown |
| `mode / credential_source` | inherit/direct/disabled；none/global/station |
| 站点 `reason` | none、disabled、material_missing、material_invalid、store_unavailable、station_unavailable |
| `attempts / busy_replies` | 可观察到的成功预览请求/忙回复计数；旧模块或复用路径无计数时为 0 |
| `remaining_ms / elapsed_ms` | 当前阶段剩余时间/从受理起累计时间；终态剩余为 0，终态计时冻结 |
| `first_request_ms / confirmed_ms / first_media_ms / sent_ms / reply_ms` | 本次受理后的相对单调毫秒；未知或早于受理的里程碑省略，客户端归一为 null |
| `preparation_ms / reply_latency_ms` | 准备时长/实际发送至匹配回复；后者必须有真实匹配回复 |
| `error / cleanup_error` | none 或 unknown；none 或 cleanup_failed |

HA 保留现有每站点锁、进度传感器 unique ID 和设备关联；只发送 station_id，按 runtime/request/station 隔离记录，清除重启前结果并阻止迟到 GET/POST 倒退已受理或终态进度。缺少/损坏/不支持的新记录不会被修补成成功。HA 本机另测 `request_to_acceptance_ms`，不跨主机相减单调时钟，不把状态轮询延迟当物理开门延迟。旧桥接器 submitted-only（以及既有 queued）响应仍兼容，缺失信息不虚构时长；明确禁用/材料不可用的新元数据会使锁控制不可用。

`03/84`、`04/89` 和 JPEG 没有本地请求 ID，不能排除所有同目标迟到流量。本地单事务、来源/目的、窗口与 generation 保护不等于线上精确事务关联。超时表示物理结果未知；`protocol_completed` 仍只表示协议确认，`physical_result_confirmed=false`，锁保持瞬时控制，不显示“门已打开”。

### 可复现离线验收与剩余边界

`tests/fixtures/station_unlock_v1.json` 来自真实 C coordinator 状态及 `df_runtime_unlock_emit_operation`，包含受理/准备、等待回复、完成、拒绝、准备超时、回复超时、取消与重启。host 测试封装与 ubus 相同的 JSON 字段形状；并非编译过的 libubus handler。C 的 `python3 -B -m unittest tests/test_unlock_fixtures.py` 比较实时 serializer 输出与 golden，并经真实 CGI 验证八种状态透传；HA 仓库持有同一 golden，独立 CI 校验解析、缺失计时、错误身份和不泄漏凭据，不依赖相邻 checkout。

本轮还覆盖 current-send 后无效 JPEG、stale-generation 状态/停止、wrong-source/destination/一秒窗口、清理失败保留文件、跨站点隔离和接管后 publication input。root-owner guard 另以不启用测试放宽宏的真实存储代码验证非 root 所有的 0600 文件被拒绝。完整 C、LuCI、HTTP、package、HA Python/Node/static/JSON 检查记录在本轮验收证据中；负向 fixture 日志及 macOS linker deprecation warning 属已记录输出，不能称输出无噪声。

本机缺少 OpenWrt SDK 与 libubus/libubox/UCI headers，未下载或安装依赖，真实 `DF_WITH_UBUS` 编译/handler、目标 ACL、浏览器/路由器应用、真实 go2rtc 发布及物理开门仍待独立验收。后续只有另行授权的有限实机比较，才能报告无会话/已有会话的准备和协议往返分布；本轮不声称延迟收益，也未推送、PR、发布、安装、Actions 轮询或更新 HACS。

## HTTP 接口

CGI 基础路径为 `/cgi-bin/doorfast`。下表中的 `GET`/`POST` 是客户端调用约定；PCM、monitor 和带 runtime/station_id 的 v1 音频读取会校验 `REQUEST_METHOD`。普通 status、控制、视频和旧式音频快照请求只按路径分派，不能把表中的方法写法当作安全边界。

| 方法和路径 | 用途 |
|---|---|
| `GET /api/v1/status` | 转发 `ubus doorfast status` |
| `POST /api/v1/answer` | 接听；请求 JSON 与 ubus 参数相同 |
| `POST /api/v1/hangup` | 挂断 |
| `POST /api/v1/unlock` | 开锁 |
| `POST /api/v1/call_elevator` | 向上/向下召梯 |
| `GET /api/v1/video/latest.jpg?generation=N` | 当前 generation 的最新 JPEG，支持 ETag |
| `GET /api/v1/audio/latest.wav?generation=N&after=R` | 增量 WAV；`after` 是上一 revision |
| `GET /api/v1/audio/latest.wav?runtime=R&station_id=S&generation=N&after=P` | v1 连续音频读取；匹配当前 talking 呼叫身份 |
| `POST /api/v1/audio/session?runtime=R&generation=N` | 获取独占 PCM producer lease |
| `POST /api/v1/audio/submit.pcm?runtime=R&generation=N&sequence=S` | 提交 1–5 个 320 字节 PCM 帧 |
| `POST /api/v1/audio/session/end?runtime=R&generation=N` | 结束 PCM producer session |
| `POST /api/v1/monitor/start` | 启动主动预览 |
| `POST /api/v1/monitor/stop` | 停止匹配 generation 的预览 |
| `POST /api/v1/monitor/viewer` | 报告观看者进入或离开 |
| `GET /api/v1/monitor/status` | 获取媒体状态 |

PCM 为 little-endian signed 16-bit、单声道、8 kHz；每帧 160 样本/320 字节。session 打开后返回 `audio_session`，提交和关闭请求通过 `Authorization: DoorfastAudio <audio_session>` 传递。仍兼容旧的 `X-Doorfast-Audio-Session`；两种音频凭据同时提供时必须一致，否则返回 `400 invalid_request`，不会发送 PCM 或关闭 session。uhttpd 的 CGI 请求头白名单不转发自定义音频头，因此 HA 后端会同时发送两种头；升级时先更新 Doorfast，再更新 HA 集成。凭据不得放入 URL 或日志。lease 为 2 秒，每次成功提交会续期；序号必须连续，部分批次成功会返回已接受前缀和下一序号，调用方应从该位置恢复。

HA 网桥 URL 中已有 HTTP Basic 凭据时，会保留旧音频头并将 `Authorization` 留给 HTTP 认证，以兼容原本转发自定义头的服务器；受 Basic 保护的 uhttpd 无法同时承载这两种凭据，本补丁不解决该部署方式的音频上行。

HTTP producer 同一时间只允许一个请求在途。请求超时或失败后必须丢弃该旧请求的完整 body，既不重放已确认的前缀，也不重放未确认的后缀；先按服务返回的 `next_sequence` 对齐，再从新采集的音频继续。`runtime_id` 或 generation 改变时必须立即结束采集并清空本地缓冲。

### 读取连续下行音频 v1

先读取 `/api/v1/status` 的 `runtime_id`、`call.station_id`、`call.generation` 与 `audio.pcm_stream_version=1`。`call.station_id` 来自主呼叫的 peer 与唯一启用站点映射，可在 ringing/talking 状态提供；音频 HTTP v1 读取只接受 talking。身份未知、映射不唯一或呼叫已结束时，客户端应停止播放并清空队列。

请求参数 `runtime` 必须为 16 位小写十六进制运行 ID；`station_id` 为 1–32 个小写 ASCII 字母、数字或下划线，首字符必须是字母。`generation` 为非零十进制 uint64，`after` 可省略或为包含零的十进制 uint64；数值必须采用无前导零的标准写法。出现 runtime 或 station_id 即选择 v1 模式，两个身份字段与 generation 必须同时提供；只带 generation/after 的旧式读取仍可用。客户端应按十进制字符串或可精确保存 uint64 的类型维护 generation、revision 和 timestamp，不要转换成会损失精度的浮点数。

以下是文档用合成身份示例；运行时用刚读取到的状态值替换查询参数：

```sh
curl -D - -o /dev/null 'http://doorfast.local/cgi-bin/doorfast/api/v1/audio/latest.wav?runtime=0123456789abcdef&station_id=gate_main&generation=7&after=0'
```

省略 after 时选择最新已发布块；连续读取时将成功响应的 `X-Doorfast-Audio-Revision` 作为下一次 after。revision 是累计真实接收包数，补偿样本不增加它，不应推断 revision 必须加一。每个 WAV 包含独立的 44 字节头，载荷为 s16le、8 kHz、单声道；最多 1600 样本/3200 PCM 字节，总长度不超过 3244 字节。

200 响应带 `X-Doorfast-Runtime-Id`、`X-Doorfast-Station-Id`、`X-Doorfast-Generation`，以及下列以 `X-Doorfast-Audio-` 为前缀的头：

| 头后缀 | 含义 |
|---|---|
| `Previous-Revision` / `Revision` | 所选块的真实包计数游标边界 |
| `Dropped-Bytes` | 为选择最新尾部而丢弃的 A-law 样本字节数，包含补偿样本；不是 PCM 字节数 |
| `Discontinuity` | 0 或 1；大缺口或丢弃积压时为 1 |
| `Timestamp-Ms` | 所选块在守护进程单调时钟上的发布时刻 |
| `Latest-Timestamp-Ms` | 复制后再次读取状态得到的最新块发布时刻 |
| `Format` / `Sample-Rate` / `Channels` | `s16le` / `8000` / `1` |

`Latest-Timestamp-Ms - Timestamp-Ms` 表示所选块相对最新发布块的积压，不是精确采集时刻或端到端时延，也不能跨 runtime 比较。ETag 保持 `df-audio-<generation>-<revision>` 形式；当前游标或匹配所选块的 ETag 返回 304，并携带三个身份头。客户端缓存和队列必须同时绑定 runtime、station_id、generation，身份切换时丢弃旧 ETag 与 after。

| 状态 | v1 JSON code | 客户端处理 |
|---|---|---|
| 400 | `invalid_request` | 修正参数或方法 |
| 409 | `audio_identity_mismatch` | 重新读取状态并清空旧播放队列 |
| 409 | `audio_cursor_expired` | 丢弃过期 after，重新从最新块接续 |
| 404 | `audio_unavailable` | 当前身份尚无就绪块，稍后重读 |
| 503 | `audio_temporarily_unavailable` | 状态/文件/发布元数据暂不可用，稍后重读 |

v1 在复制 WAV 后再次核对呼叫身份，防止切换或重启时返回旧身份音频；身份未证明的错误不带身份头。音频 ready 与身份分别检查，结束的呼叫不能靠旧文件继续读取。客户端仍需处理网络等待期间发生的身份变化。

本地低层入口也可发送单帧：

```sh
# stdin 必须恰好为 320 字节 PCM
doorfast-pcm-submit "$generation" < frame.pcm
```

本地入口是非阻塞 Unix datagram socket `/var/run/doorfast-audio.sock`，每个 datagram 必须恰好 336 字节：偏移 0 是 8 字节 `DFPCM01\0`，偏移 8 是 8 字节 little-endian 非零 generation，偏移 16 是 320 字节 PCM。服务每次调度最多检查 32 个 datagram，并保留消息边界。创建端只删除既有 socket，不会替换普通文件；发送端必须核对目标确为当前有效用户拥有、权限为 `0600` 的 socket。

## 音视频和主动预览

### 通话媒体

- UDP/8302 接收 G.711 A-law，支持 128/160/192 字节等不超过 256 样本的载荷。16 位 sequence 按半范围窗口回绕；最多四个等待槽，缺口等待 40 ms，满窗可提前推进，重复/迟到包不进入 PCM。缺口按最近真实包长度补 `0xd5`，每次最多 320 样本；更大缺口标记 discontinuity。
- 待发布样本达到 800，或最早 pending 样本等待 100 ms 后发布；每块最多保留最新 1600 样本，超量计入 dropped 并标记 discontinuity。发布失败保留 pending，至少等待 100 ms 后重试。这些是调度条件，实际执行时刻取决于主循环，不是实机时延保证。
- 原子发布的增量 WAV 最多保留 20 块；generation 切换、挂断、超时和关闭会清空重排、缓冲与旧块。真实 packets/bytes 与含补偿的 sample_count/concealed_samples 分开统计，状态通过 `audio.pcm_stream_version=1` 宣告 v1。
- 本地 `/var/run/doorfast-audio.sock` 只接受当前 talking generation 的 160 样本 PCM，编码后发往已观察到的对端音频路由。
- UDP/8303 只接纳当前 generation 且来源/目标匹配的媒体，完成 JPEG 分片重组、结构校验和尺寸提取。
- 最新 JPEG 默认发布到 `/tmp/doorfast-latest.jpg`；HTTP 读取时再次核对 generation、帧号和长度，避免读取到切换中的文件。

上行 PCM → A-law → UDP/8302 已完成单元和 VM 验证；下行音频与通话视频目前由主机测试、HTTP fixture 和 PCAP 交叉验证覆盖，尚未完成整条 VM 媒体注入。真实扬声器播放、麦克风回声、时延、抖动及长时间稳定性尚未通过实体设备验收。

### 主动预览到 Home Assistant

主动预览要求：

1. 主机模式已启用。
2. 安装 `doorfast-media`。
3. 从抓包或物业设备表获得精确门口机逻辑地址；它不能由室内机 GVS 身份推导。
4. Home Assistant/go2rtc 已预创建与 `media_stream_name` 相同的 RTSP 发布入口。
5. LuCI 中配置 go2rtc 主机、端口、编码器、帧率、码率和凭据。

go2rtc 最小配置示例：

```yaml
streams:
  doorfast_preview:

rtsp:
  listen: ":8554"
  username: doorfast
  password: ${DOORFAST_RTSP_PASSWORD}
  default_query: "video=h264"
```

Doorfast 需要访问 Home Assistant/go2rtc 的 `8554/TCP` 来发布 RTSP；HA WebRTC 查看端通常还需要访问 `8555/TCP+UDP`。go2rtc 的 `1984/TCP` 是管理 API，应保持鉴权并仅限管理网络访问。

运行链路为：发送 `03/04` → 等待 `03/84` → 接收 JPEG → 启动受监督的 FFmpeg → 通过 RTSP/TCP 发布 H.264 → HA 由 go2rtc 提供 WebRTC。来电会先抢占主动预览；停止时发送 `03/02`，处理 `03/82` 或本地超时，并回收 FFmpeg 子进程。

编码器选项：

- `auto`：探测启动环境后一次性选择 Intel QSV，其次 VAAPI，否则选择软件 libx264；已选 FFmpeg 启动失败时不会继续尝试其他编码器。
- `software`：libx264，`veryfast` + `zerolatency`。
- `vaapi`：`h264_vaapi`，使用 `/dev/dri/renderD128`。
- `qsv`：`h264_qsv`。

分辨率支持源尺寸、480x640、360x480 和 240x320；帧率支持 5、8、10、12、15 FPS；目标码率允许 256–2000 Kbps；Profile 支持 Baseline 和 Main。凭据保存在 `/etc/doorfast/media-credentials`，页面不会回显已保存值，修改后需重启 Doorfast 才进入运行模块。

当前 ABI v3 为每个启用的门口机维护独立媒体会话，并允许在 `media_max_encoders` 配置上限内并发发布来源流；同一门口机的多名观看者仍由 go2rtc 共享一条来源流。下列 LuCI/UCI 选项已传入媒体模块并实际影响运行行为：

- `media_min_free_kib`：发送预览请求前读取 `/proc/meminfo` 的 `MemAvailable`，不足时以 `resource_exhausted` 拒绝启动。
- `media_preview_timeout`：单次预览达到总时限后进入停止状态并发送协议停止请求。
- `media_first_frame_timeout`：门口机确认预览后等待首个 JPEG 帧的超时。

`media_max_encoders` 控制同时运行的来源编码器上限，`media_incoming_call_policy` 可选择在容量已满时抢占最早的主动预览或保留现有预览并返回 `capacity_busy`。FFmpeg 子进程异常退出只会使对应会话失败并回收其资源。请求间隔 1000 ms、最多 3 次请求和停止确认超时 1000 ms 仍为固定协议常量。

仓库的软件验收先通过 SSH 检查 VM 中 Doorfast 与媒体模块可用，再在 host 上编译与 `doorfast-media` APK 相同的 ABI v3 源码集合。host harness 只通过导出的 `df_media_module_api_v3` 执行 `start/tick/receive_control/push_jpeg/command`：它核对每条 preview 发出的 `03/04`，注入测试用匹配 `03/84`，启动两个独立编码器，并确认 stop 与 preview 抢占均发出 `03/02`。RTSP sink 验证 `doorfast_gate_main` 与 `doorfast_gate_side` 的并发 producer 峰值精确为 2；fake ffmpeg 只记录每个 encoder pipe 的字节数和 SHA-256，用不同 JPEG 标记验证数据没有串流，不保存原始 JPEG：

```sh
python3 -B tests/run_doorfast_vm_multi_media.py \
  tests/fixtures/fake-vm-preflight-ssh.sh
```

这只确认 host 上真实 ABI v3/session manager 在合成控制回复和 JPEG 输入下的并发会话、容量策略、encoder pipe 隔离、进程回收和报告脱敏；它没有向 VM daemon 注入媒体流，也不确认 VM 或实体门口机接受这些控制包。实体门口机的并发验收仍为 pending：必须取得双向现场 PCAP，证明两个门口机同时成功完成独立的 `03/04` 会话，并从不同来源产生 UDP/8303 视频流，才能将设备侧并发标记为已确认。

## Home Assistant 集成

配套集成位于 [jieinfo/doorfastforha](https://github.com/jieinfo/doorfastforha)。HA 负责展示、交互和下发指令；ImmortalWrt 负责门禁协议、媒体接收、编码和转发。

配套 HA 的站点状态展示读取各 `media.sessions[].station_id`：`ready` 表示发布链路可用，`source_live` 才表示真实视频源在线；冻结帧继续发布期间应显示重连而非实时画面。`started_ms/first_frame_ms` 是同一次尝试的单调时钟毫秒值，不是墙上时间；两者有效时可计算该次尝试的首帧延迟，不能当成从用户点击起的总等待时间。状态实体不启动预览、不改变冷却或重试频率。

### 呼叫事件 relay

主守护进程在 `/var/run/doorfast/events.sock` 发布以下 JSONL 事件：

- `incoming_call`
- `call_established`
- `hangup`
- `timeout`
- `preempted`

事件包含 `schema_version=1`、单调递增的 `event_id`、`generation` 和 `timestamp_ms`。socket 最多连接 16 个客户端，每个客户端有 64 条有界队列；慢客户端不会阻塞呼叫主循环。

独立的 `doorfast-event-relay` 负责断线重连、有界队列和指数退避。配置示例：

```uci
config relay 'main'
	option enabled '1'
	option url 'http://homeassistant.local:8123'
	option entry_id '0123456789ABCDEF0123456789ABCDEF'
	option token ''
	option token_file '/etc/doorfast/ha-token'
	option ca_file '/etc/ssl/certs/ca-certificates.crt'
```

`url` 只填写 HTTP/HTTPS authority，可带端口，不填写 `/auth/login_flow` 或其他路径。最终地址固定为 `<url>/api/doorfast/<entry_id>`。HTTP 可用于可信内网；HTTPS 会校验 CA 和主机名。LuCI 中输入长期访问令牌后，init 脚本将其移动到 root 所有且权限不超过 `0600` 的 `token_file`，并从 UCI 删除明文。

事件推送用于降低延迟，HA 状态轮询仍是丢事件和重连后的权威兜底。

媒体不再使用独立的事件 relay。Doorfast 通过现有事件 relay 发布来电、通话和预览状态事件；go2rtc 只负责接收 Doorfast 发布的媒体流并向 Home Assistant 提供 RTSP/WebRTC。媒体配置、RTSP 凭据和编码参数位于独立的 LuCI“媒体预览”页面。

## LuCI 页面

| 页面 | 当前功能 |
|---|---|
| 状态 | 每 5 秒读取 ubus；显示同步、呼叫、开锁、电梯、音频、视频和媒体运行状态 |
| 门口机 | 主机模式下扫描候选、人工采用配置、显示候选和已配置站点的路由新鲜度 |
| 媒体预览 | 配置门口机逻辑地址、组播/RTSP、编码器、帧率、码率、内存阈值和预览超时 |
| 来电自动化 | 开关“来电自动向上召梯”和“来电自动解锁”；默认关闭 |
| 部署配置 | 清晰分离被动接口与主机接口；配置 GVS 身份、室内机 IP/掩码、门禁材料和上行接口 |
| HA 事件 relay | 配置 HTTP/HTTPS 地址、配置项 ID、长期令牌和 CA 文件 |
| 日志 | 显示守护进程内存日志；内部最多保存 128 条，重启清空 |

LuCI 刻意不提供接听、挂断、开锁和召梯操作页。这些动作由 HA 或受控本地 API 执行，避免在路由器管理界面重复一套控制入口。LuCI 的状态和日志不替代抓包或实体动作确认。

## 透明串联和取证

`/etc/config/doorfast-deployment` 描述管理员已经创建的透明桥：

```uci
config inline 'main'
	option enabled '0'
	option recording_enabled '0'
	option bridge 'br-door'
	option upstream 'eth2'
	option downstream 'eth3'
	option management 'br-lan'
	option observation 'eth2'
	option evidence_root '/mnt/doorfast'
	option recent_budget_mib '14336'
	option control_budget_mib '8192'
	option log_budget_mib '1024'
	option reserve_mib '6144'
```

先运行只读预检：

```sh
doorfast --preflight /etc/config/doorfast-deployment
```

返回规则：

- 退出 0 且 `"safe":true`：当前快照满足检查项。
- 退出 2：配置可读取，但现场条件不安全，JSON 列出全部失败原因。
- 退出 1：无法读取配置或系统证据。

预检核对独立管理口、无地址桥、成员关系、carrier、路由、防火墙、DHCP/RA、抓包能力、存储挂载与空间。它不修改系统。默认要求 `/mnt/doorfast` 是独立持久挂载，首次部署至少 30 GiB 总容量和 29 GiB 可用空间。

现场首次串联必须按以下顺序执行：

1. 核对接口、MAC 地址与机箱面板标识，确认门禁上游、原 MT8157 下游和独立管理口没有接反。
2. 保持管理口可达，并准备一根可立即恢复原 MT8157 物理直连的回退网线。
3. 由管理员提前建立无 IP、无 DHCP/RA 的透明桥，再运行上述只读预检。
4. 首次插入透明桥时保持 Doorfast 和记录器停止，先验证原 MT8157 的上线、来电、接听、开锁和电梯行为均未改变。
5. 为避免桥成员上的重复包，只选择一个 observation 接口。
6. 先启动记录器，再启用 Doorfast 被动模式；连续观察至少 7 天后才评估主动主机模式。
7. 任何来电、控制、在线状态或网络行为异常时，立即停止 Doorfast 并恢复原物理直连。

记录器默认关闭且不由主服务自动启动：

```sh
/etc/init.d/doorfast-recorder start
```

它生成有界轮转的 recent/control PCAP、脱敏元数据和状态文件；剩余空间达到保留阈值后进入 `space_guard` 并停止写入。站点前后快照可用于证明 Doorfast 没有改动网络配置：

```sh
doorfast-site-inventory /mnt/doorfast/inventory
python3 scripts/compare-doorfast-site.py BEFORE_DIR AFTER_DIR ROLES.json
```

仓库保留一份非 Markdown 的 VM 证据样例：`docs/evidence/2026-09-15-vm-gvs-talking.json`。

## CLI 工具

```text
doorfast --config <path> [--call-elev <0|1>]
doorfast --preflight <path>
doorfast --preflight <path> --root <fixture-root>
doorfast --import-legacy <doorlink-uci>
doorfast --inspect-pcap <capture.pcap> <IS-address>
doorfast --simulate-handshake-pcap <capture.pcap> <IS-address>
doorfast-recorder --config /etc/config/doorfast-deployment
doorfast-recorder --self-test <directory>
doorfast-pcm-submit GENERATION [SOCKET]
```

`--import-legacy` 只把旧 Doorlink UCI 转换成一份待人工审核的 Doorfast 起始配置并输出到 stdout；它不会写系统，也不会导出激活码、云令牌、Webhook 或更新设置，所有主动自动化保持关闭。

`--inspect-pcap` 和 `--simulate-handshake-pcap` 是离线分析工具，不发送网络流量，也不能单独证明现场兼容性。`scripts/check_gvs_hybrid.py` 会在临时副本中插入明确标记的合成帧，用于敏感性实验，合成结果不是缺失原始报文的证据。

## 协议证据索引

厂商资料与授权 PCAP 不存入本仓库。复核时把数据集根目录设为 `$MT8157_DATASET`；以下 SHA-256 是当前协议判断使用的固定基线。

| Evidence | 相对路径 | SHA-256 | 主要用途 |
|---|---|---|---|
| E-001 | `pcap/gvs-session-20260907.pcap` | `2690ccad19d9f19a210080cf5891cbde3c2a6144389fb80c2137af2a13c0ad89` | 通话、音频、视频和控制时序 |
| E-002 | `pcap/gvs-active-three-20260907.pcap` | `06e1993ac20ac9157110ff3dcb5667c7dcbe909e5ae5bac456ce2f8cd32f726c` | 主动控制和直接开锁样本 |
| E-003 | `pcap/gvs-inbound-call-20260907.pcap` | `70fd6ddc63eead07ae8bd39f8d68458e9110b456f33d16de5cb57259949c82fb` | 入站呼叫期间流量边界 |
| E-004 | `pcap/gvs-incoming-three-20260907.pcap` | `dfc712339604ea90427c4a900f40570acfbcf0ddbeafb5a40ca0cb9db3e7e367` | 多次来电和控制关联 |
| E-005 | `docs/doorfast-disconnect-20260911.pcap` | `c2a1ad01474665c430232e86083cd939ef2aa5c9fad28e6856ffb85a506ad2a7` | 断线和截断抓包边界 |

由这些原始证据、厂商 APK 静态路径与当前实现交叉得到的结论：

- 五份音频抓包共有 14,959 个 UDP/8302 包，其中 13,804 个完整、1,155 个受抓包长度限制而截断；完整 payload 为 128、160 或 192 字节。原 MT8157 发出的 6,245 个包全部使用 160 字节 payload，支持 Doorfast 上行采用 160 样本/20 ms，并要求下行兼容三种长度。
- 五份视频抓包共有 56,759 个 UDP/8303 包，其中 26,773 个完整、29,986 个被截断；完整分片使用 1200 字节 capacity，长度、分片数、索引和偏移关系一致。
- 多份抓包合计出现 6 个 `04/09` 直接开锁请求和 7 个 `04/89` 成功结果。这证明线上报文形状和协议结果，不证明门锁实际动作。
- 电梯样本包含 5 个 `08/02` 和 2 个 `08/03`，没有 `08/82` 或 `08/83`；因此电梯回复和实体动作仍缺少现场证据。
- E-003 本身没有捕获到最初的 `03/01`，来电入口由同期 logcat 和静态调用路径支持，不能写成 PCAP 已证明初始来电请求。`03/81` 是来电通知回复，不是来电请求。
- 厂商 APK 静态路径把 `03/50` 命名为 `sendBusy`；当前只能把它作为该路径的控制响应处理，不能用它证明会话已经建立。

复核路径：先校验原始文件哈希，再按控制、音频和视频端口读取时间线，最后把观察结果与当前解析器测试及现场实体结果分别记录。

2026-10-12 交接包的字段勘误与时序复核见 [portable-package 协议基线](docs/gvs-portable-package-baseline.md)。其中的源／目的地址标签、JPEG 补头选择偏移和 assets 名称不能直接照抄报告；本轮不替换已经通过抓包交叉验证的收发协议。

```sh
export MT8157_DATASET=/absolute/path/to/mt8157
shasum -a 256 "$MT8157_DATASET"/pcap/*.pcap
tcpdump -nn -r "$MT8157_DATASET/pcap/gvs-session-20260907.pcap" \
  'udp port 8300 or udp port 8302 or udp port 8303'
```

仓库中的 `docs/evidence/2026-09-15-vm-gvs-talking.json` 是可机读的合成 VM 验收样例；它不替代上述外部原始证据，也不替代实机闭环。

保留的 Markdown 证据附件包括：来电抓包缺口与控制面更正、同步协议逆向、音视频 PCAP 验证、公共头和 native 库来源分析、VM UDP 验收、保活回放验收、旧 Doorlink 来源边界、厂商数据参考矩阵及同步分析范围。每份附件开头都声明其证据快照属性；其中出现的旧版本号、历史实现状态和历史下一步均由本 README 覆盖。

## 构建与验证

主机需要 C17 编译器、libpcap 开发文件、libcurl 开发文件、Python 3、Node.js 和 shell 工具。

```sh
make -B test
build/doorfast-tests
make -B doorfast recorder peer-sim peer-udp-inject pcm-submit pcm-http-test
python3 -B -m unittest discover -s tests -p 'test_*.py'
```

连续音频组合与兼容性回归可在仓库根目录复现；`make test` 在产物已是最新时不会再次运行 C runner，因此显式执行一次：

```sh
set -e
make test
build/doorfast-tests
make doorfast
sh tests/test_doorfast_http.sh
sh tests/test_package_manifest.sh
node tests/test_luci_status.js
node tests/test_luci_media.js
node tests/js/test_media_status.mjs
git diff --check
```

只定位接收组合测试时，在构建后运行 `build/doorfast-tests --audio-receive-pipeline`。真实 serialize/parse/jitter/buffer/playout/decode 处理合成载荷，fake publisher 只保留计数、元数据与 PCM 摘要，不写音频文件；测试断言样本内容、数量、回调时刻、序号回绕、generation 隔离、1600 样本上限与发布失败重试。对应证据与现场边界见 [音频 PCAP 附录](docs/gvs-audio-pcap-validation.md)。

CI 还执行 LuCI/Node、CLI、包清单、HTTP、站点清单和 PCM fixture 验证。可在本地逐项运行：

```sh
node tests/test_luci_status.js
node tests/test_luci_settings.js
node tests/test_luci_media_view.js
node tests/js/test_media_status.mjs
sh tests/test_main_cli.sh
sh tests/test_recorder_cli.sh
sh tests/test_pcm_submit_cli.sh
sh tests/test_pcm_http_cli.sh
sh tests/test_site_inventory.sh
sh tests/test_package_manifest.sh
sh tests/test_doorfast_http.sh
```

需要 VM 时，由调用者提供 SSH 包装脚本；测试不会猜测目标地址：

```sh
python3 tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh
python3 tests/run_gvs_vm_ubus_call.py /absolute/path/to/vm/ssh.sh
python3 tests/run_doorfast_vm_preflight.py /absolute/path/to/vm/ssh.sh
python3 tests/run_doorfast_vm_inline_bridge.py /absolute/path/to/vm/ssh.sh
python3 tests/run_doorfast_vm_recorder.py /absolute/path/to/vm/ssh.sh
python3 tests/run_doorfast_vm_event_relay.py /absolute/path/to/vm/ssh.sh
python3 tests/run_doorfast_vm_pcm_http.py /absolute/path/to/vm/ssh.sh
python3 tests/run_doorfast_vm_media.py /absolute/path/to/vm/ssh.sh
python3 tests/run_doorfast_vm_stations.py /absolute/path/to/vm/ssh.sh
```

测试证明的范围必须按“证据等级”解释。VM JSON 证明合成来电控制、保活、PCM ingress、A-law 编码和 UDP/8302 发送。下行音频与通话视频由主机测试、HTTP fixture 和 PCAP 交叉验证覆盖，并未完成整套 VM 媒体注入。主动预览的本地 fixture 验证 RTSP 发布与抢占；VM 的主动预览用例只验证软件包安装、状态结构、进程数量和网络状态未改变。UDP loopback、RTSP fixture 或合成 PCAP 通过，不等于门口机、门锁、电梯、扬声器或物业平台已经实机通过。

## 辅助模块的真实状态

仓库还保留 SIP parser、策略、审计和网段重叠诊断模块，并有单元测试；发现候选模块已经接入生产 `runtime_service` 和 LuCI 站点页面：

- SIP parser 能识别 `INVITE`/`BYE` 和 `Call-ID`，但当前 GVS 主运行链路不依赖 SIP。
- discovery 已接入主机模式生产扫描、候选 ubus/LuCI 工作流和已配置站点路由；候选仍需人工采用，不会自动持久化。
- policy 能对来电给出 notify/hangup/delayed-open 决定，audit 能写 syslog，但当前主动控制由 GVS 状态机和 HA 入口驱动。
- diagnostics 能判断两个 IPv4 网段是否重叠，但尚未接入 LuCI 或启动门禁。

除主动发现外，下面列出的辅助模块仍只能记为库能力，不能列为用户可用的生产功能。

## 已知限制和未完成项

### 连续音频与 Home Assistant

- Doorfast v1 下行 API 与原 20 ms PCM 上行入口已有合成软件回归。HA 端持续解码/播放、麦克风采集、缓冲策略及 runtime/station/generation 切换处理仍需独立实现和验收。
- 目标 ImmortalWrt/ubus 构建集成、连续收发长时间稳定性、真实扬声器听音、双向回声和端到端时延未由本轮主机测试验证；HTTPS 代理、认证与证书部署也未验收。
- 后续 HA 验收应分别记录 API/身份与游标行为、HTTPS 连接结果和现场听音结果；只保存计数、摘要和元数据，不记录原始语音。发布积压指标不能替代采集到扬声器的时延测量。

### 在线身份维护

- `07/01`、`91/01` 和 `91/02` 经 UDP presence emitter 发送；收到 `07/01` 后，reply queue 使用同一个运行时厂商头提供器和真实 UDP socket 发送 48 字节 `07/81`。
- 主机模式启动时，只把非空的 `sync_mini1_secretkey`、`sync_mini2_secretkey` 配置加入同步 store。维护者的 `periodic_sync` 按每包最多 20 项构造并发送 `91/03`；空 store、`peer_online` 和 `peer_offline` 属于零包动作，不会伪造同步键值。
- `sync_action` 日志中的 `sent` 只在实际发出至少一个 UDP 包时为 1，并同时记录 `packets=N`，不再把零包动作记作已发送。
- 上述生产路径已有本地 UDP 和序列化测试，但尚未取得现场物业系统冷启动认可、周期在线维持及双向 PCAP 的 L4 证据。

### 主动发现的软件验收

- VM/fake-SSH 验收已经证明一次扫描生命周期的三帧 `07/06` 计划/发送计数、三条候选、两条已配置站点、`runtime_id`/revision 以及 60 秒路由新鲜度字段能够稳定读取；真实 VM 返回计划帧数，fake fixture 额外暴露已发送帧数。
- 验收同时比较扫描前后的网络 UCI，确认发现接口不会修改网络配置；组播验收记录 GVS 身份推导地址和最终生效地址。
- fake fixture、软件 VM 或回放数据都不能证明物业系统接受身份注册，也不能证明实体门口机、门锁或电梯动作。

### 实体设备闭环

以下项目尚无充分 L4 证据：

- 物业系统冷启动识别、分配/确认逻辑身份和持续在线。
- 当前现场的门口机来电送达与 `03/81` 回执已经完成；接听后的长时间稳定通话、断线恢复和媒体质量仍缺少完整记录。
- `04/89` 协议完成后门锁实际动作。
- `08/82`/`08/83` 的完整现场样本，以及电梯实际响应。
- 门口机主动预览 `03/04`/`03/84` 与持续视频取流。
- 两个门口机同时成功完成各自的 `03/04` 会话，并由不同来源发送独立 UDP/8303 视频流；此项必须用双向现场 PCAP 验证，当前仍为 pending。
- 真实音频播放、麦克风上行、回声、延迟和断线恢复。
- go2rtc/WebRTC 在目标 Home Assistant 上的长时间观看和多次启停。

### 媒体资源策略

- ABI v3 按站点隔离 monitor、JPEG 队列和 encoder，`effective_capacity` 取已配置编码器上限与启用站点数的较小值。多个 HA 观看者仍由 go2rtc 共享同一站点来源流，不会额外占用编码器槽位。
- 最低可用内存、预览总时限、首帧超时、最大编码器数和来电容量策略已经进入 ABI v3 并由 LuCI 配置；诊断开关和发布重试次数仅为旧配置解析兼容，不改变运行。
- host ABI v3 harness 已通过真实 monitor 状态机验证 `03/04`、合成 `03/84`、`03/02`、两条 encoder pipe 的独立摘要、并发峰值 2、独立停止、满容量保留或抢占以及进程回收；VM 仅接受安装状态预检。它不证明 VM daemon 或实体门口机执行了并发监控。
- 编码器和 go2rtc 端到端失败处理已测试，但未在真实门口机视频流上调优。

### 接口边界

- LuCI 不包含控制按钮；控制入口属于 HA/受控 API。
- HTTP CGI 没有独立用户认证，网络访问控制由 OpenWrt/uhttpd 和部署拓扑承担。
- 普通 status、控制和快照 CGI 路由尚未强制校验 HTTP method；只有 PCM 与 monitor 路由执行 method 检查。
- generation 在进程重启后从 1 重新计数；普通控制已经强制匹配 `runtime_id`。旧版客户端若不提交该字段会被拒绝，必须升级配套 HA 集成。
- 普通控制学习到的 peer IPv4 与主动预览路由都执行 60 秒新鲜度限制；普通路由失效后回退到逻辑身份推导地址，因此现场仍需验证地址变化时的实际可达性。
- 内存日志容量为 128，重启即丢失；页面不会提供长期审计存储。
- HTTP/ubus 返回的是接收、排队、提交或协议状态，永不代表实体动作已确认。

## 下一步开发顺序

本轮 P0 在线发送、P1 控制可靠性和 P2 媒体配置一致性已经完成代码接线与自动化测试。后续按证据缺口推进：

1. 在 ImmortalWrt VM 安装同一次 CI 生成的 `doorfast r54`、`doorfast-media r54` 和 `luci-app-doorfast r17`，验证升级、ABI v3 加载、ubus 参数拒绝、零包 `sent=0 packets=0` 与真实 UDP 发包。
2. 现场抓取冷启动和至少两个同步周期的双向 PCAP，确认 `07/01 → 07/81`、`91/01`/`91/02` 与分片 `91/03` 被物业系统接受，并核对源 IP、目标 IP、端口和厂商头。
3. 使用新版 `doorfastforha` 依次验证 Doorfast 重启、runtime 切换、来电、接听、挂断、开锁和向上/向下召梯，分别记录协议提交、协议确认和实体结果。
4. 软件侧多门口机会话和可配置并发已完成；现场继续验证两个门口机各自成功完成 `03/04`，并通过双向 PCAP 确认 UDP/8303 来自两个独立来源。
5. 验证主动预览、通话视频和双向音频，再根据实体流量调整内存阈值、超时、码率、缓冲和回声处理；确认可靠的异步发布重启策略前不重新暴露发布重试选项。

## 后续现场回归和验收

- 来电送达与 `03/81` 回执已经在当前现场完成，门口机不再显示“无应答”，不再作为待解决问题。
- 针对新接入的 `07/81` 和 `91/03` 生产发送，重新执行“冷启动 → 长时间在线 → 来电 → 应答 → 挂断”双向抓包，目的是确认在线维护代码没有造成回归，并验证不同冷启动条件，不是再次解决“无应答”。
- 分别验证开锁、向上/向下召梯和电梯状态，分开记录协议确认与实体结果。
- 验证主动预览、通话视频和双向音频，再根据实测调整超时、码率、缓冲和回声处理。
- `doorfastforha` PR #22 已完成原生 WebRTC camera、monitor generation、viewer 生命周期和断线清理；剩余工作是在目标 Home Assistant 与实体门口机上完成整套控制、视频和音频验收，而不是继续实现同一套 WebRTC 契约。

## 维护规则

- 只在本 README 维护项目总设计和进度，不新增平行状态、路线图或设计 Markdown。证据附件可以保留，但必须标明采集日期、来源、哈希或复现路径、可证明边界，并声明当前状态以本 README 为准。
- 可以保留机器可读的 JSON、PCAP 摘要和测试 fixture，但其中不得包含真实住址、住户身份、令牌、密码或未脱敏原始内容。
- 新功能合并前必须更新能力矩阵、配置/API、证据等级、限制和下一步。
- 每项协议结论至少说明代码路径、可复现测试和材料/PCAP 依据；只有现场实体结果才能标记 L4。
- 厂商材料与逆向结果互相交叉验证，冲突时保留原始证据并以现场抓包为最终判据。
