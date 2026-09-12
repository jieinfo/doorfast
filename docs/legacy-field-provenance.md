# 来源边界更正：Doorlink 与 GVS/MT8157 不关联

本记录用于纠正来源边界，不提取密钥、不生成认证值，也不用于向真实门禁发送报文。

## 已确认事实

- 旧包核心文件是 MIPS32 little-endian ELF：`/usr/bin/doorlink`，属于独立的旧 Doorlink 项目。
- 该 IPK 与 Doorfast、GVS 协议、MT8157 室内机 APK 没有已确认的代码、协议或依赖关系。
- 即使该 IPK 中出现 `AES_*`、`GVS801`、`GVSGVS`、`random`、`encryption` 等字符串，也不能将它们解释为 GVS/MT8157 公共头字段来源。
- `doorlink` 的登录/授权数据只能作为 Doorlink 自身行为的证据，不能迁移到 Doorfast。
- 当前材料没有提供可用于 GVS 字段来源追踪的 MT8157 APK/so 实体文件。

## 结论

不能从 Doorlink IPK 推断 GVS 公共头字段来源。GVS 字段来源只能从 MT8157 APK/so 的实际调用链、现场报文以及授权调试输出独立确认。Doorfast 因此继续使用占位字段并保持被动模式。

## 可复核证据

- 样本路径：`work/doorlink-1907-analysis/evidence/rootfs/usr/bin/doorlink`
- SHA-256：`a255c2fadf0180d704882c2c207f0eef50b24fcac6215df16d69f34da306eb99`
- 静态命令：`file`、`rabin2 -i`、`strings -a -t x`

下一步若要继续来源追踪，需要取得有权分析的 MT8157 APK/so 或授权调试输出；Doorlink IPK 不再作为该任务的证据来源。
