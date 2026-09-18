# MT8157 厂商数据参考矩阵

> **证据附件：** 本文保留厂商数据集来源和证据强度映射，不维护当前功能、设计、配置或开发进度。当前结论以仓库根目录 `README.md` 为准。

更新日期：2026-09-13

## 1. 来源声明

本项目将用户通过 `$MT8157_DATASET` 指定目录中的全部内容视为厂商交付数据集。
该路径只用于本地开发和复核，不随 Doorfast 公共仓库提交。

“厂商交付”只描述材料来源，不表示内容必然准确，也不自动表示每份文件都是厂商
正式发布的协议规范。实现前必须用同目录材料交叉核对，并尽可能使用 PCAP、项目测试
和实体设备结果验证。例如数据集内的 `gvs-compatible-host-development-manual.md` 明确
把自身定义为兼容性
开发规格。Doorfast 因此同时记录材料类型和证据强度，避免把静态推导、单次抓包或
虚拟机自洽测试扩大为所有设备都支持的厂商承诺。

| 标识 | 材料类型 | 可支持的结论 |
|---|---|---|
| `V-S` | 厂商交付数据集中的静态调用链、资源或结构记录 | 字段布局、分支、常量及本地状态机 |
| `V-C` | 厂商交付数据集中的现场抓包 | 指定设备、版本和场景实际出现的线上行为 |
| `V-D` | 厂商交付数据集中的兼容规格或汇总文档 | 设计导航；关键字段须回溯到 `V-S`/`V-C` |
| `D-T` | Doorfast 单元、回放、虚拟对端或 VM 测试 | 项目实现自洽和目标系统可运行 |
| `D-F` | Doorfast 与用户自有实体设备的受控闭环 | 指定安装环境中的互操作结果 |

发布能力至少需要 `V-S` 或 `V-C` 支持协议实现，并由 `D-T` 验证代码。涉及开锁、
电梯、双向媒体等实体动作时，还必须逐项取得 `D-F`；“已发送”不能写成“已完成”。

## 2. 数据集基线

本轮采用以下厂商交付数据：

| 数据 | 本地相对位置 | 识别信息 |
|---|---|---|
| 静态分析索引 | `docs/Snippet/INDEX.md` | 001–029，当前目标为 `com.moorgen.zigbee.gateway_1784259851932.apk` |
| 分析状态 | `docs/Snippet/STATE.md` | 最后完成记录 029；列出仍未闭合的操作码和结果矩阵 |
| 兼容开发规格 | `docs/gvs-compatible-host-development-manual.md` | 版本 1.4；作为 `V-D` 导航使用 |
| 会话抓包 | `pcap/gvs-session-20260907.pcap` | SHA-256 `2690ccad19d9f19a210080cf5891cbde3c2a6144389fb80c2137af2a13c0ad89` |
| 主动三段抓包 | `pcap/gvs-active-three-20260907.pcap` | SHA-256 `06e1993ac20ac9157110ff3dcb5667c7dcbe909e5ae5bac456ce2f8cd32f726c` |
| 来电抓包 | `pcap/gvs-inbound-call-20260907.pcap` | SHA-256 `70fd6ddc63eead07ae8bd39f8d68458e9110b456f33d16de5cb57259949c82fb` |
| 三次来电抓包 | `pcap/gvs-incoming-three-20260907.pcap` | SHA-256 `dfc712339604ea90427c4a900f40570acfbcf0ddbeafb5a40ca0cb9db3e7e367` |
| 电梯与离线场景抓包 | `docs/doorfast-disconnect-20260911.pcap` | SHA-256 `c2a1ad01474665c430232e86083cd939ef2aa5c9fad28e6856ffb85a506ad2a7`；含 5 个 `08/02` 和 2 个 `08/03` |

哈希用于发现本地材料被替换或混用。真实地址、密钥、媒体和完整报文继续保留在
本地数据集，不进入公共提交和普通运行日志。

## 3. 目标功能与实现依据

| 用户目标 | 厂商数据依据 | 已确认协议/行为 | Doorfast 接入边界 | 当前缺口 |
|---|---|---|---|---|
| 呼叫推送 | `010-talkback-business.md`、`026-gvs-activity-intent-eventbus.md`、来电 PCAP | `03/01` 建立来电；每次有效通告回复 `03/81`；事件含来源、类型、方向和会话状态 | 接收器建立带 generation 的会话；事件层输出脱敏来电；回执作为独立事务发送 | 仍需实体门口机确认公共头和回执接受性 |
| 通话接听与挂断 | `010-talkback-business.md`、来电/会话 PCAP | 接听 `03/03 -> 03/83`；挂断 `03/02 -> 03/82`；`03/51`/`03/52` 为通话保活 | 所有请求绑定 generation、目标地址和目标 IP；超时、拒绝、迟到回复分别处理 | 忙线、转接和跨固件结果矩阵仍需补证 |
| 门禁控制 | `009-entrance-guard-unlock.md`、`029-gvs-entrance-guard-payload-result.md` | 直接路径 `04/09 -> 04/89`；挑战路径 `04/11 -> 04/91`；直接帧保留“声明 12、实际 body 8”兼容形状 | 历史实现完成构帧和结果关联；安装密钥不入日志；只有终端回复可形成协议成功/失败；当前状态见根目录 `README.md` | 失败码全集、挑战异常和物理门锁动作需 `D-F` |
| 召梯 | `015-elevator.md`、`docs/doorfast-disconnect-20260911.pcap` | `08/02` 是 42 字节公共头加 4 字节 payload，总长 46 字节；上/下方向；立即发送、1000 ms 后最多重试一次、2000 ms 硬截止；`08/82` 只结束协议事务 | 46 字节构帧、UDP 发送、主机模式 `call_elevator`、来电固定上行自动召梯和单槽控制器已通过 `D-T`；请求、协议结果、超时和实体结果分开报告 | 尚无 `08/82` 现场样本和 `D-F`，不能宣称召梯成功 |
| 电梯状态 | `015-elevator.md`、`docs/doorfast-disconnect-20260911.pcap` | `08/03` 是 42 字节零 payload 查询；UI 约 1 秒周期调用；`08/83` 返回数量及楼层/状态二元组 | 42 字节构帧、主机模式每秒查询、守护进程接收路径及最多 8 项的 `1 + 2 * count` 有界解析已通过 `D-T`，ubus/LuCI 发布状态年龄和原始值 | 尚无 `08/83` 现场样本和 `D-F`；`08/01` 语义未闭合 |
| 视频功能 | `010-talkback-business.md`、`012-video-business.md`、`017-camera-input.md` | UDP/8303；JPEG 以最多 1200 字节分片；片号从 1 开始且连续；会话结束释放资源 | 先实现接收、校验、重组和会话隔离，再接入浏览器/HA 媒体桥；保存图片必须由显式策略触发 | 类型头映射、丢片策略、真实延迟和端到端画面需 `D-F` |
| 双向音频 | `010-talkback-business.md`、`011-audio-business.md`、`016-audio-pipeline.md`、`019-g711-g726-native.md` | UDP/8302；8 kHz 单声道 G.711；典型网络 payload 160 字节 | 音频收发和抖动缓冲绑定 generation；停止通话清空旧媒体 | 编解码模式、回声消除和实际延迟需设备验证 |
| 通知推送 | `003-notification.md`、`013-call-records.md`、`023-backend-binder.md` | `06/01` 通知、`06/81` 回执；序列去重；可按文件序号关联图片 | 转为 Doorfast 本地事件，再由 HA/MQTT/Webhook 适配器消费；核心不等待上层通知成功 | 927 字节记录与图片传输需要真实样本回归 |
| 身份与在线 | `005-indoor-sync.md`、`007-sdk-registration.md`、`018-sdk-init-registry.md`、`027-gvs-floor-address-mutation.md` | `0x61` 室内机和 `0x62` MiniOS 同户节点；在线、选举、同步与逻辑地址/IP 派生相关 | 地址过滤、路由观察、在线期限和同步事务作为全部主动功能的前置条件 | 跨固件公共头生成和冷启动身份冲突需现场确认 |

“电梯控制”目前只包含厂商数据已经闭合的召梯和状态查询。厂商数据没有闭合任意
楼层选择控制帧，因此项目不得自行猜测或借用其他协议的楼层命令。

## 4. 可复核消息样例

厂商数据中的电梯记录给出以下脱敏 payload：

```text
08/02 declared_length=04 00 body=00 10 16 01
```

按静态调用链和现场帧交叉解码为：`direction=0x00`（下行入口），其余三个位置为
`g[3]` 的 BCD 解码值、原始 `g[3]`、`g[4]`。例如原始 `0x16` 解码后是十进制 16，
线上字节为 `0x10`。这里保留位置含义，不把匿名化值解释成用户楼层。

直接开锁请求的结构为：

```text
04/09 declared_length=0c 00 body=<8-byte installation access material>
```

有效 UDP 长度为公共 42 字节加 8 字节 body。Doorfast 必须保留该兼容形状，且
不得在文档、测试输出或日志中提交真实安装材料。

本地只读复核命令：

```sh
export MT8157_DATASET=/absolute/path/to/mt8157
shasum -a 256 "$MT8157_DATASET"/pcap/*.pcap
make -B doorfast
./build/doorfast --inspect-pcap \
  "$MT8157_DATASET/pcap/gvs-inbound-call-20260907.pcap" \
  IS:2-1-101-1
```

最后一个地址是本地测试身份，执行时应换成该抓包对应的脱敏测试身份。复核输出只
证明 Doorfast 能解析这份厂商交付样本，不等于主动控制通过。

## 5. 后续开发规则

1. 新增协议行为时，在实现文档或 PR 中列出本矩阵中的厂商数据文件和证据类型。
2. 字节布局优先回溯到 `docs/Snippet/NNN-*.md` 的静态来源及对应 PCAP；汇总手册
   只用于导航。
3. 厂商静态行为与现场抓包冲突时，保留两种输入及兼容开关，不静默改写证据。
4. 单元测试使用匿名化最小帧；真实地址、密钥、音视频和完整 PCAP 留在本地。
5. 状态接口区分 `queued`、`sent`、`confirmed`、`completed`、`rejected`、
   `expired` 和 `unverified`。
6. 每项主动能力按 `V-S/V-C -> D-T -> D-F` 推进。未取得 `D-F` 时，在 UI、HA
   实体和发布说明中显示“未完成实体设备验收”。
