# 旧 Doorlink 公共头字段来源审计（静态）

本记录只说明字段的代码来源线索，不提取密钥、不生成认证值，也不用于向真实门禁发送报文。

## 已确认事实

- 旧包核心文件是 MIPS32 little-endian ELF：`/usr/bin/doorlink`。
- 在该 IPK 中没有名为 `libencrypt.so` 的独立文件；包内只有核心程序、Lua 配置、两个压缩的监视器和启动脚本。
- 核心程序导入表/字符串同时出现 `AES_set_encrypt_key`、`AES_set_decrypt_key`、`AES_ecb_encrypt`、`AES_encrypt` 等 OpenSSL AES 符号，以及 `GVS801`、`GVSGVS`、`random`、`encryption` 等字符串。
- `random` 和 `encryption` 出现在设备登录 JSON 的字段上下文中；这证明存在登录/设备认证数据结构，但不能证明它们就是 GVS 公共头中的两个 8 字节字段。
- 现有静态材料没有提供 `IndoorSyncBusiness`、`TalkBackBusiness` 或 MT8157 APK 的可执行文件，因此无法仅凭旧 IPK 确认室内机模块如何生成公共头字段。

## 字段来源分类

目前只能把来源分为三类候选：

1. 核心程序内部的 OpenSSL 加密调用；
2. 设备登录或配置数据中的随机/加密字段；
3. GVS 报文构造时从会话或入站帧复制的字段。

在获得对应调用链、输入输出样本和合法测试向量前，不能把其中任何一类认定为真实公共头认证算法。Doorfast 因此继续使用占位字段并保持被动模式。

## 可复核证据

- 样本路径：`work/doorlink-1907-analysis/evidence/rootfs/usr/bin/doorlink`
- SHA-256：`a255c2fadf0180d704882c2c207f0eef50b24fcac6215df16d69f34da306eb99`
- 静态命令：`file`、`rabin2 -i`、`strings -a -t x`

下一步若要继续来源追踪，需要取得有权分析的 MT8157 APK/so 或厂商调试输出；仅凭当前 IPK 无法把上述候选收敛为可验证的真实字段生成路径。
