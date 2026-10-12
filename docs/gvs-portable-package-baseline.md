# GVS portable-package 协议复核记录（2026-10-12）

> **证据附件：** 本文只记录厂商交接材料与既有解析器的交叉复核。
> 项目能力、配置和接口仍以根目录 `README.md` 为准。
> 本次没有发送现场控制报文，也没有取得新的物理开门或双向音频验收结论。

## 材料身份与边界

交接包 `portable-package` 的入口是 `README.md`、
`report/2026-10-12_gvs-intercom-portable-report.md`、`source/smali/` 和 `tools/`。
它是既有 APK 的移植分析材料，并非新的门口机固件或新的线上协议版本。
APK SHA-256 为
`6793c5777bea2c4f56f30c79c19d37d9089976eaaaa61c6da0ef6724a9a8487f`。

交接包引用的四份原始 PCAP 与根 README 的 E-001 至 E-004 哈希相同；
PCAP 没有复制到交接包中。交接包的 `MANIFEST.sha256` 是 CSV 格式的清单，
不能直接按传统 `shasum -c` 输入使用。
厂商 `.so` 是 Android arm64 库，不是可直接部署到 ImmortalWrt x86_64 的媒体模块。

源文件、抓包、完整 JPEG、原始音频及安装材料保留在用户本地数据集中，
本仓库只保存脱敏结论与合成回归测试，不复制厂商库或安装密钥。

## 不能照抄报告的字段

偏移均相对于 UDP payload，数字字段是 little-endian。以下结论以实际方向、
静态发送链和 Doorfast 收发测试共同约束，不以报告或工具的变量名为准。

| 字段／路径 | 复核后的基线 | 交接报告中的问题 |
|---|---|---|
| 公共地址偏移 10、16 | 10 是目的六字节逻辑地址，16 是源地址 | 第 12.1 节把源、目的写反了；不能据此翻转现有解析器 |
| UDP/8302 音频 payload 起点 | 42 字节头；接收长度包含 128、160、192 字节 | 不能沿用视频的 38 字节头，不能强制全部为 160 字节 |
| UDP/8303 视频 payload 起点 | 38 字节头；片号从 1 开始；常见 capacity 1200 | 不能沿用控制／音频头长度 |
| 视频补头选择字节 | `VideoBusiness.messageDeal()` 保留 `v1=0x16`，最终以偏移 22 调用 `getJpegHead()` | 不能只按报告的偏移 23 `codec/type` 标签选择补头 |
| JPEG assets | `VIDEO_HEADER.BIN` 和 `HEADER_DQT25/30/35/40/45/50/60/70/80.bin` | `HEADER_DQT09..80.bin` 的概括不精确 |
| 普通解锁 | `04/09` 声明正文长度 12、实际材料 8 字节；`04/89` 是协议结果 | 不应“修正”线上已验证的长度兼容形状，也不能把回复等同门状态 |

四份完整视频抓包复核共重组 1,974 帧：E-001 183、E-002 409、E-003 99、
E-004 1,283；这些帧已有 JPEG SOI/EOI，未发现需要补头的样本。
因此本轮只锁定完整 JPEG 的现有路径，不凭静态分支引入新的补头资源依赖。
这一离线记录不证明现场网络不会丢帧，也不证明所有固件都使用完整 JPEG。

## 会话时序审查

- `03/04 -> 03/84` 是预览控制确认，不保证 JPEG 已到达；`03/50` 是忙响应，
  不是媒体成功。真实源在线由匹配目标、身份和会话的有效视频帧决定。
- 媒体包可能先于控制确认到达；现有门口机预览允许在对应请求期间接纳媒体，
  不新增“必须先等到 `03/84` 才收视频”的限制。
- 入站接听路径为 `03/03 -> 03/83`。厂商样本的室内机上行音频在确认后开始；
  Doorfast `gvs_audio_tx` 只在 `TALKING` 启动，停止或换 generation 后清理旧发送。
  本次没有把预览改成发送麦克风音频。
- `03/02` 与 `03/82` 的停止握手、已有用户停止冷却、首帧前忙重试、
  短流重连和来电优先策略保持原样。状态展示不能反向改变控制时序。
- RTSP 发布器仍运行且 `ready=true` 可以只是冻结帧；`source_live=false` 时
  不能报告真实画面在线。旧桥缺少此字段时应显示未知。

## 回归锚点

| 复核边界 | 仓库测试 |
|---|---|
| 目的／源地址、音频 42 和视频 38 字节头 | `tests/test_gvs_media.c` |
| 合并视频 datagram、分片和 JPEG 边界 | `tests/test_gvs_media.c`、`tests/test_gvs_video_reassembly.c`、`tests/test_gvs_jpeg.c` |
| 首帧前忙回复、提前视频、停止回复与冷却 | `tests/test_gvs_monitor.c` |
| 冻结发布与真实源区别、generation 和来电抢占 | `tests/test_media_session.c`、`tests/test_media_session_manager.c` |
| 通话音频发送启动、停止和 generation 隔离 | `tests/test_gvs_audio_tx.c`、`tests/test_gvs_pcm_ingress.c` |
| 解锁目标、准备预览和最近协议结果归属 | `tests/test_runtime_ubus.c`、`tests/test_gvs_access.c` |

这些测试证明实现与脱敏模型一致，不取代实体设备验收。
