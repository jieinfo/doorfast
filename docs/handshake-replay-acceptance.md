# 保活状态查询与离线回放验收（r27）

真实 UDP 发送保持关闭。LuCI 新增“保活模拟（仅内存发送）”，展示启动状态、未回复次数、距下次探测的最近采样值、事务状态和进程生命周期内累计丢弃次数。旧服务不返回这些字段时不显示该区块。

ubus 现有状态响应的 `call` 表新增 `handshake_mode`、`handshake_active`、`handshake_missed`、`handshake_next_ms`、`handshake_dispatch`、`handshake_dropped`。只读 ACL 沿用现有状态接口；没有新增控制动作。`handshake_next_ms` 为相对最近采样时刻的毫秒值，不是墙钟时间。丢弃计数饱和于 UINT64_MAX，重启清零。

## 复现

```sh
make -B test doorfast
node tests/test_luci_status.js
build/doorfast --inspect-pcap /tmp/doorfast-1901-three-calls.pcap IS:2-1-1901-1
build/doorfast --simulate-handshake-pcap /tmp/doorfast-1901-three-calls.pcap IS:2-1-1901-1
```

新命令走与守护进程相同的离线控制器；包间空档按最多 100 ms 步进，活动结束后跳至下一个包。定时器先于同刻接收处理。EOF 不增加时间。原 `--inspect-pcap` 行为保持不变。两种模式各自独立运行，不把模拟帧放入原始抓包。

## 本地现场证据结果

2026-09-11 对三份已有现场抓包回放；文件存在重叠时间范围，不把统计相加当作独立实验次数。

| 抓包 | 接受保活消息 | 模拟内存帧 | 模拟保活断开 | 会话期限超时 |
| --- | ---: | ---: | ---: | ---: |
| doorfast-1901-three-calls.pcap | 85 | 98 | 0 | 4 |
| doorfast-1901-reboot-operation.pcap | 85 | 98 | 0 | 4 |
| doorfast-disconnect-20260911.pcap | 88 | 110 | 0 | 4 |

前两份位于本机 `/tmp`，第三份位于仓库旁 `doorfast-field-artifacts`。原始抓包不加入仓库。

SHA-256（依表顺序）：

```text
18d1d30fd1035696bd5dfbc515b4a9288eccc8ce53d723326679ac53bbb95bee
82d3f5851423fd389182e92ff458dfab3b1953f47529f000b686bcae8b0cf08d
c2a1ad01474665c430232e86083cd939ef2aa5c9fad28e6856ffb85a506ad2a7
```

Evidence：上述本地文件和两种 CLI 输出；Finding：当前接收、计时规则可以处理这些已记录的保活消息，模拟没有提前产生保活断开；Path：构建通过后进行虚拟机 ubus/LuCI 验收，再升级现场被动观察版本。

限制：抓包中的回复是 MT8157 原有交互产生的，不是对模拟内存帧的真实回应。因此结果不能证明门口机接受 Doorfast，也不能验证主动断线超时阈值。`invalid` 是现有提取器/接收器拒绝总数，包含不相关流量及地址/状态不匹配，不能当作链路损坏计数。当前三份材料均未形成 talking 转换；媒体与完整通话链路尚未验收。

合成 PCAP 回归另外覆盖无回复时跨报文空档产生五次探测、第六周期断开，以及 EOF 不触发断开。C 状态查询测试覆盖丢弃计数和剩余时间，JavaScript 测试覆盖展示值、旧响应兼容和无效计数拒绝。
