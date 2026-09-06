# Doorfast

Doorfast 是面向 x86_64 ImmortalWrt 25.12.1 的原生门禁网络观察与集成服务。
它以透明抓包、SIP 会话归一化、脱敏审计和事件通知为起点，替代旧 Doorlink
部署中的非开源、MIPS 专用和授权依赖部分。

> 当前版本是基础阶段，默认不发送开门、挂断、召梯或楼层控制报文。没有实体设备
> 的开发环境可以完整验证配置、PCAP 回放、解析和策略，但不能凭空证明现场控制兼容性。

## 当前能力

- 构建 x86_64 ImmortalWrt APK；目标为 25.12.1。
- 读取并校验 Doorfast 的基础配置模型。
- 使用 libpcap 对受管理员选择的接口进行受限 SIP 捕获。
- 将 INVITE、BYE 等 SIP 会话归一化为不含原始报文的事件。
- 生成经管理员审批才可使用的发现候选项。
- 对自动化策略生成可审计的“允许 / 拒绝 / 延迟”决定。

## 不会做的事

- 不包含 Doorlink 的程序代码、激活机制、激活码、供应商云部署或远程脚本执行。
- 不启用未被用户自有设备证据验证的门锁、电梯、楼层控制。
- 不暴露未认证的本地 HTTP 控制接口。
- 不采集或提交真实住址、住户号码、密码、令牌、视频或完整原始 SIP 报文。

## 构建与测试

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

## 为新品牌贡献证据

请阅读 [贡献与协议证据规范](CONTRIBUTING.md) 和
[兼容性矩阵](docs/compatibility.md)。只接受可复现、脱敏且来自贡献者自有设备的
数据包样本；控制支持必须同时具有请求、响应和现场结果证据。
