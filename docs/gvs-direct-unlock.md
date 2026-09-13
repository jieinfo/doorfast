# GVS 直接门禁事务 `04/09 -> 04/89`

更新：2026-09-13。

## 厂商数据和交叉验证

实现依据 MT8157 厂商交付数据集中的：

- `docs/Snippet/009-entrance-guard-unlock.md`：直接与挑战路径、地址类型和结果事件；
- `docs/Snippet/029-gvs-entrance-guard-payload-result.md`：精确线上布局和 PCAP 对齐；
- `pcap/gvs-active-three-20260907.pcap`、`gvs-incoming-three-20260907.pcap`、
  `gvs-session-20260907.pcap`：6 个直接请求；
- 四份 PCAP 中的 7 个 `04/89` 成功结果。

静态记录与 PCAP 对直接请求的异常长度形状一致：公共长度字段声明 12 字节，实际
body 只有 8 字节，总 UDP payload 为 50 字节。Doorfast 保留这个线上形状，不补四个
零，也不把声明长度改为 8。所有现场结果样本均为 `01`，所以其他状态只保留为未知
失败值，不赋予未经验证的错误名称。

## 本轮实现

`gvs_access` 提供两个独立边界：

1. 准备并序列化活动会话中的直接开锁请求；
2. 在有限时间内关联严格反向地址的 `04/89` 单字节结果。

请求必须满足：

- 当前会话处于预览、响铃或通话状态；
- 调用方 generation 等于当前会话 generation；
- 目标地址类型为厂商材料列出的 `0x32`、`0x13` 或 `0x62`；
- 本机、目标地址和 8 字节安装材料完整提供。

安装材料由上层安全配置提供。本模块不会选择 MiniOS 密钥名称，不会把固定材料写入
生产默认配置，也不会记录材料内容。

结果状态为 `waiting`、`completed`、`rejected`、`expired` 或 `cancelled`。只有来源、
目标、family、opcode、长度、会话代次和时间窗口全部匹配时才接受结果。原始状态
`01` 映射为协议完成，其他值映射为协议拒绝并保留 raw status。

`physical_result_confirmed` 不会由 GVS 协议回复自动置真。物理门锁是否动作必须在实体
设备验收记录中单独确认。

## 尚未包含

- LuCI 开门按钮；
- `04/11 -> 04/91` 随机挑战事务；
- 无活动呼叫时按静态配置目标开门；
- 实体门锁闭环和失败码含义。

## 运行接口

现在守护进程通过真实 UDP 发送 `04/09`，复用已观察到的对端地址路由及厂商公共头。
配置 `/etc/config/doorfast` 的 `access_material` 为经现场验证的 16 位十六进制字符串
（8 字节）。默认空字符串禁用开门，非法长度或字符拒绝加载；不提供厂商示例默认值。
文件应仅允许 root 读写（权限 600）。材料不会返回到 ubus 状态或应用日志。

启用 `active_host` 后，先读取 `ubus call doorfast status` 中 `call.generation`，再调用：

```sh
ubus call doorfast unlock '{"generation":7}'
```

这里的 7 必须替换为当前活动通话的 generation。被动模式、未配置材料、旧会话、
非门口机目标或重复调用都会拒绝。返回 `submitted: true` 仅说明 UDP 发送成功。
`status.access` 提供 configured、state、generation，收到回执后提供 raw_status；
physical_result_confirmed 始终为 false。

等待回执窗口暂定 1000 毫秒，这是工程参数，尚需现场验证。超时不重发。
由于回执没有请求编号，每个通话最多尝试一次，包括发送失败；新通话才允许下一次。
会话结束或切换会取消等待。严格反向地址和时间窗口不能证明回执来自可信设备，
也不能完全消除不同通话间延迟回执的歧义；现场验收仍需对照抓包与门锁动作。

本轮已覆盖主机 C 测试和 UDP 回环；APK 构建后的 ImmortalWrt ubus 集成验收需使用
本轮构建产物继续执行。厂商材料中的设备类型和状态含义仍需在目标现场核验。
