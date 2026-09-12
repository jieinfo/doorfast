# MT8157 GVS 公共头字段来源（静态证据）

样本：`com.moorgen.zigbee.gateway_1784259851932.apk`  
SHA-256：`6793c5777bea2c4f56f30c79c19d37d9089976eaaaa61c6da0ef6724a9a8487f`

本记录只描述字段来源和调用链，不提取 native 密钥、不生成可被真实设备接受的认证值，也不向现场网络发送报文。

## 解析端字段布局

`com/gvs/general/protocol/f.smali` 的入站解析将完整报文按固定偏移拆分：

- `0..9`：固定头；
- `10..15`：目的地址；
- `16..21`：源地址；
- `22..29`：`randomCode`，8 字节；
- `30..37`：`encryptionCode`，8 字节；
- `38`：功能码；
- `39`：操作码；
- `40..41`：小端数据长度；
- `42..`：数据区。

## 发送端字段来源

同文件约第 811 行的私有构造方法 `a([B[BBB)I` 展示了发送端顺序：

1. 创建 `java.util.Random`；
2. 分配 8 字节数组并调用 `nextBytes`，得到本次报文的 `randomCode`；
3. 调用 `GVS_Encrypt.b([B[B)V`，也就是 Java 层 `udpEecrypt` JNI 包装，将前一个 8 字节数组转换为第二个 8 字节数组；
4. 依次写入固定头、目的地址、源地址、随机数组、转换结果、功能码和操作码。

因此可以确认：公共头两个 8 字节字段不是从 Doorlink IPK、Moorgen Windows 工具或 UCI 配置直接读取；第一个字段由每个发送报文的运行时随机数产生，第二个字段由 `libencrypt.so` 的 `udpEecrypt` native 调用派生。

## native 边界

APK 包含 `lib/arm64-v8a/libencrypt.so`，导出 JNI 包装名 `Java_com_gvs_general_encrypt_GVS_1Encrypt_udpEecrypt`，并含 `TEA_Encrypt`、`UDP_Encrypt` 等符号线索。该库负责第二字段的变换，但当前记录不展开密钥或可复现认证值。

## 对 Doorfast 的影响

- Doorfast 的 42 字节公共头偏移与 MT8157 解析端一致；
- Doorfast 以前的全零占位字段只能用于离线结构测试，不能与 MT8157 的真实发送逻辑等价；
- 真实兼容性仍需在授权隔离设备上验证 `udpEecrypt` 的合法调用结果；
- 在没有安全的 native 调用适配和现场回滚方案前，继续保持 `passive_only=1`。
