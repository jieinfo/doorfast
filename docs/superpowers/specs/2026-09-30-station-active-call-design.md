# 门口机独立主动呼叫与可视对讲设计规格

## 目标

为每台已配置门口机提供独立的主动呼叫入口。用户从 Home Assistant 选择某台门口机后，Doorfast 完成该门口机的预览升级，建立视频与双向语音，并提供独立挂断和状态反馈。

## 范围与约束

- 本期只支持已通过配置或发现登记、且 `monitorable=true` 的门口机。
- 同一时间只允许一台门口机进入正式通话；其他门口机仍可保持预览或等待重试。
- 不引入抓包中没有证据的新控制帧。主动呼叫使用已确认的 GVS 流程：`03/04` 预览、`03/84` 确认、`03/03` 升级通话、`03/83` 确认、`03/51`/`03/52` 保活、`03/02`/`03/82` 挂断。
- `03/04` 与 `03/03` 的目标必须绑定到同一个 `station_id`、逻辑地址和会话 generation，禁止跨门口机或旧会话确认。
- 视频继续使用现有 8303 JPEG 接收和 RTSP 发布；语音使用抓包已确认的 8302 G.711 A-law 双向链路。

## 协议流程

### 呼叫

1. HA 调用 `call(station_id)`。
2. Doorfast 检查该门口机路由、可监视状态和全局通话占用；不满足时返回结构化错误 `station_unavailable` 或 `busy`。
3. 如果该门口机没有有效预览会话，启动该 station 的 `03/04`；收到匹配的 `03/84` 后才继续。
4. 发送 `03/03`，等待同一 station、同一 generation 的 `03/83`。超时返回 `ack_timeout`，并结束本次升级，不影响其他 station 的预览。
5. 确认后将会话状态设为 `talking`，启动 8302 音频收发、`03/51`/`03/52` 保活和视频复用。

### 挂断

1. HA 调用 `hangup(station_id)`，只作用于指定 station；未指定时仅在兼容旧 API 的全局挂断入口中作用于当前通话。
2. Doorfast 发送 `03/02`，等待匹配的 `03/82` 或本地挂断超时。
3. 无论确认是否到达，都停止该 station 的音频、保活和通话媒体资源，并保留现有预览冷却策略，避免立即重复发送 `03/04`。

## Doorfast 核心接口

- 在现有 station 会话管理器上增加 `call(station_id)`、`hangup(station_id)` 和按 station 的通话状态查询。
- 会话目的区分 `preview` 与 `talk`; 通话升级不得创建第二个未绑定的视频会话。
- 对外 HTTP/ubus 接口提供：
  - `POST /api/v1/call`，请求 `{ "station_id": "..." }`；
  - `POST /api/v1/hangup`，请求 `{ "station_id": "..." }`；
  - `GET /api/v1/status` 返回全局通话和每个 station 的状态、generation、错误码。
- 返回错误必须稳定可判断：`busy`、`station_unavailable`、`ack_timeout`、`media_failed`、`invalid_station`。
- 现有无 station 参数的 `answer`/`hangup` 保持兼容，并映射到当前唯一正式通话；新 HA 集成使用按 station 接口。

## Home Assistant 集成

- 每个 station camera 旁注册独立 `Call` 与 `Hang up` button entity，entity 唯一 ID 包含 station ID。
- 新 button 调用对应 station 的 API；禁止通过全局 `answer` 选择目标。
- coordinator 保存每个 station 的 `preview`/`calling`/`talking`/`ended` 状态和错误原因，并在状态事件或轮询刷新时更新实体。
- 视频继续由该 station 的 WebRTC source 提供。
- 通话时增加该 station 的 8302 入站音频播放通道，并复用现有麦克风上行通道；挂断或媒体失败时关闭两个方向的音频。
- UI 文案提供英文与简体中文：`Call`/`呼叫`、`Hang up`/`挂断`、`Calling`/`呼叫中`、`Talking`/`通话中`、`Busy`/`占线`、`Station unavailable`/`门口机不可用`。

## 状态与错误处理

每个 station 使用以下互斥状态：`idle`、`preview`、`calling`、`talking`、`ending`、`failed`。全局通话锁记录当前 station 和 generation。

- 预览确认超时：`calling` → `failed(ack_timeout)`。
- 门口机不可达或路由过期：`calling` → `failed(station_unavailable)`。
- 已有其他 station 通话：立即返回 `busy`，不改变目标 station 的预览。
- 视频或音频无法建立：`talking` → `failed(media_failed)`，同时发送挂断并释放资源。
- 收到旧 generation、其他 station 或重复确认：忽略并记录 station ID，不改变当前状态。

## 测试与验收

### 核心单元测试

- 精确验证 `03/04`、`03/03`、`03/02` 帧的目标 station 和 payload。
- 验证只有匹配 station/generation 的 `03/84`、`03/83`、`03/82` 才能推进状态。
- 验证通话锁、超时、占线、不可用和媒体失败路径。
- 验证挂断会停止音频、保活和视频资源，并保留预览冷却时间。

### HA 测试

- 三台 station 各自生成独立 button 和状态；调用一个 station 不影响另外两个。
- button 请求 payload、错误映射、状态刷新和中英文翻译。
- 通话建立后能接收 8302 音频并发送麦克风音频；挂断后音频任务完全结束。

### 实机验收

在不修改持久化 station 配置的前提下，逐台执行一次呼叫、保持至少 10 秒、挂断，并抓取 UDP/8300、8302、8303。验收标准是：控制帧顺序与上述流程一致，视频持续发布，音频双向有包，挂断后无残留保活或音频任务；失败时 HA 显示具体错误而不是无限等待。

### 本地 Task 6 验证边界（2026-09-30）

核心 host 测试已覆盖逐 station 的帧构造、station/generation 绑定、通话锁、占线、确认超时、旧确认拒绝、保活归属以及挂断后的音频和媒体清理。现有 `tests/run_gvs_vm_ubus_call.py` 只提供单个 synthetic peer 的 legacy `answer`/`hangup` VM 检查；它没有可注入每个 station 的 `03/84`、`03/83`、`03/82` 和保活回复的接口。因此没有新增猜测性的 `run_station_call_acceptance.py`，也没有把 VM 或 fixture 结果表述为实体门口机验收。

三台 station 的完整控制帧顺序、UDP 8300/8302/8303 观察、真实 HTTP 状态以及实体设备音频双向性仍需在不改持久化配置的实机阶段通过抓包验证。
