# Doorfast

现场串联学习部署请先阅读 [现场部署步骤](docs/doorfast-inline-field-deployment.md)。
APK 不会创建透明桥；软件桥或整机故障会中断 MT8157，必须保留人工直连回退线。

协议开发以用户指定的 MT8157 厂商交付数据集为依据。数据来源、功能映射和证据
使用规则见 [MT8157 厂商数据参考矩阵](docs/mt8157-vendor-reference-matrix.md)。
本地厂商数据不提交到公共仓库，项目只提交可复核的来源标识、匿名化字段和测试。

Doorfast 是面向 x86_64 ImmortalWrt 25.12.1 的原生门禁网络观察与集成服务。
它以透明抓包、GVS 会话归一化、脱敏审计和事件通知为起点，替代旧 Doorlink
部署中的非开源、MIPS 专用和授权依赖部分。

> 默认保持被动观察；启用 `active_host` 后可发送已进入事务队列的控制、在线和同步报文。没有实体设备
> 的开发环境可以完整验证配置、PCAP 回放、解析和策略，但不能凭空证明现场控制兼容性。

## 当前能力

最终目标和阶段验收见[完整主机模式与 APK 项目规划](docs/doorfast-host-mode-roadmap.md)。
按当前已合并代码逐项核对的结果见[当前能力与验证状态](docs/current-capability-status.md)。
当前主程序已具备常驻服务、主动 UDP 控制和同步、呼叫信令、直接开锁、手动及来电自动召梯、视频快照、音频接收和上行音频核心；所有主动动作和媒体仍需逐项完成实体设备验收。

- 已有 x86_64 ImmortalWrt 25.12.1 APK 构建配方与 CI；当前代码的目标安装运行仍需阶段验证。
- 读取并校验 Doorfast 的基础配置模型。
- 可通过 `--config` 对管理员明确选择的物理口、VLAN、bridge 或 bond 持续捕获 GVS 控制流量；接口名称不会预设为 `eth0` 或 `wlan0`。
- 实时服务与离线回放共用 GVS 接收事务处理器，可观察来电、摘机交换、进入通话、时间同步、挂断、振铃抢占和空闲超时；主机模式还可发送来电回执、接听、挂断与保活报文。
- 运行中捕获失效会结束当前会话并进行 5 次有限退避重开；连续失败后退出并由 procd 按策略处理。
- 已实现 GVS 公共头解析、逻辑身份过滤及结束原因统计，并有合成和混合回放测试。
- 直接开锁、手动向上/向下召梯、来电自动向上召梯和电梯状态查询已经接入运行服务；协议完成与实体动作确认分别报告。
- UDP/8302 音频接收、增量 WAV 发布、会话绑定的 PCM 到 G.711 A-law 上行发送，以及 UDP/8303 JPEG 分片重组和最新画面发布已经接入运行服务。
- 生成经管理员审批才可使用的发现候选项。
- 对自动化策略生成可审计的“允许 / 拒绝 / 延迟”决定。

## GVS 接口配置

软件包默认保持停用，也不会猜测本机接口。管理员需要为门禁网络和（可选）上行网络
显式选择可用的本机接口名，例如物理口、VLAN、bridge 或 bond：

```uci
config gvs 'main'
	option enabled '0'
	option gvs_interface 'br-door'
	option gvs_local_address 'IS:2-1-101-1'
	option uplink_interface 'br-lan'
	option passive_only '1'
	option active_host '0'
	option capture_promiscuous '0'
```

被动观察时保持 `passive_only '1'`。P1 离线/受控主机模式可设置 `active_host '1'`，并将
`enabled` 改为 `1`；该模式会启用 UDP/8300 主动发送，并按
[厂商公共头算法](docs/gvs-vendor-header-provider.md)为每个报文生成随机字段及其变换值，
但仍必须在隔离网络和目标设备逐项验收后再接入生产。`gvs_local_address` 使用
`IS:楼栋-单元-房间-分机` 格式，例如 `IS:2-1-101-1`；它仅用于本机入站帧筛选。
Doorfast 不会修改网络、路由或防火墙；主动发送仅针对由会话状态机提交的控制事务。
修改 `/etc/config/doorfast` 后执行 `/etc/init.d/doorfast reload` 会停止旧实例并按新配置启动。

## 透明串联部署预检查

APK 会安装一份默认关闭的 `/etc/config/doorfast-deployment`。填写实际的无地址网桥、
门禁上联口、MT8157 下联口和独立管理口后，运行：

```sh
doorfast --preflight /etc/config/doorfast-deployment
```

检查通过时输出 `"safe":true` 的 JSON 并返回 0；配置有效但现场条件不安全时输出全部
失败原因并返回 2；无法读取或计算状态时返回 1。检查只读取 sysfs、进程、UCI、挂载和
接口地址，不创建网桥，不修改接口、路由、防火墙或 DHCP/RA，也不会自动启动记录。
`/mnt/doorfast` 必须是独立持久挂载，首次部署需至少 30 GiB 总容量和 29 GiB 可用空间。

## 不会做的事

- 不包含 Doorlink 的程序代码、激活机制、激活码、供应商云部署或远程脚本执行。
- 不启用未被用户自有设备证据验证的门锁、电梯、楼层控制。
- 不暴露未认证的本地 HTTP 控制接口。
- 不采集或提交真实住址、住户号码、密码、令牌、视频或完整原始 GVS 报文。

## 构建与测试

离线检查命令为 `./build/doorfast --inspect-pcap capture.pcap IS:2-1-101-1`，其中地址应替换为测试环境配置。
目前只输出解析和会话统计；它尚不代表完整来电流程或主机模式互操作验证。

在具备 libpcap 开发文件的主机上执行：

```sh
make test
sh tests/test_main_cli.sh
sh tests/test_package_manifest.sh
```

要从旧 Doorlink UCI 文件生成一份可人工审核的 Doorfast 起始配置：

```sh
doorfast --import-legacy /etc/config/doorlink
```

该命令只输出配置，不会写入路由器；不会导出激活码、云令牌、Webhook 或更新设置，
并且始终将开门、挂断、召梯自动化选项设为禁用。

GitHub Actions 使用官方 ImmortalWrt 25.12.1 x86_64 SDK 构建 APK。SDK 压缩包会在
每次使用前校验 SHA-256；CI 产物只包含 `doorfast-*.apk` 本体。

## 协议支持范围

Doorfast 仅维护项目自身验证过的 GVS 协议适配。它不接受新增品牌、第三方协议
适配或第三方控制证据；所有发布的行为说明与匿名化测试夹具均由项目维护。
每项新增协议行为必须引用厂商数据参考项，并分别记录项目测试与实体设备验收；
不能用虚拟机或合成帧通过代替门锁、电梯或音视频对端确认。
