# ARM64 libencrypt.so 静态依赖分析

日期：2026-09-08。范围：用户提供的摩根 MT8157 Android APK 解包材料；本次未运行库、连接设备、生成认证字段或修改程序校验逻辑。

## 结论

该库具有两条不同的上层调用链：公共 UDP 头字段生成，以及本机 MCU 设备校验。后者确实影响原 Android SDK 初始化。两者均不能直接等同于旧 Doorlink IPK 的激活授权。完成此库分析也不等于完成独立室内机主机模式。

## E1：二进制身份与依赖

主样本：`$MOOGREN_DECODED/lib/arm64-v8a/libencrypt.so`，其中 `MOOGREN_DECODED`
指向本地合法取得的 MT8157 APK 解包目录。

SHA-256：`c172c2f65a4a87158c7b72fc90ef157e759cf5c6e84f5f2b9ca89eef0689d99f`。

大小 6408 字节，ELF64 little-endian AArch64 shared object，已剥离普通符号。与 `work/moorgen-control-apk/apktool_out/lib/arm64-v8a/libencrypt.so` 副本哈希一致，不据此推断其他版本一致。

动态依赖：`liblog.so`、`libc.so`、`libm.so`、`libstdc++.so`、`libdl.so`。

动态导入：`__cxa_atexit`、`__cxa_finalize`、`__android_log_print`、`malloc`、`free`。表中未见直接网络收发导入，但这不能证明整个 Android 应用没有在线校验。

动态导出保留 `Tranverse32`、`TEA_Encrypt`、`TEA_Decrypt`、`UDP_Encrypt`、`printfbytes` 及三个按名称绑定的 JNI 包装函数。JNI 名称分别以 `teaEncrypt`、`teaDecrypt`、`udpEecrypt` 结尾，最后一个拼写来自原样本。未见 `JNI_OnLoad` 导出。

检查方法：`file`、`shasum -a 256`、`/usr/bin/objdump -T`、`/usr/bin/objdump -p`，辅以局部反汇编和 smali 引用检索。未包含固定密钥值或设备认证字段生成配方。

## E2 / F1：UDP 封装调用链

以下路径相对于主样本解包目录的 `smali_classes3/`：

- `com/gvs/general/encrypt/GVS_Encrypt.smali`：加载 `encrypt`；方法 `b([B[B)V` 转发到 native `udpEecrypt`。
- `com/gvs/general/protocol/c.smali`，约 211–234 行起的公共头构造方法：创建两个 8 字节缓冲区，对其中一个填充随机数据，调用上述包装，再将两部分写入公共头。
- `com/gvs/general/protocol/f.smali`，约 700–772 行：接收端分别保存 `RandomCode`、`EncryptionCode`，随后解析功能码、操作码、长度和数据。

发现：此前统一称为“不透明区”的公共头 22–37 字节，至少在这份 SDK 中有两个各 8 字节字段的明确语义。UDP 包装路径不调用 TEA 包装，不能将整个 UDP 载荷描述为 TEA 加密。

限制：接收解析器存储字段，不等于所有后续消费者都不验证字段；未分析到的门口机实现也可能进行验证。不能据此声称全零字段可用、认证可省略或设备必然接受独立发送端。

## E3 / F2：MCU 校验与 SDK 初始化

- `com/gvs/vdp/mcu/g/a.smali`，约 293、323 行：调用 `GVS_Encrypt.a`，即 native `teaEncrypt`，参与带定时器和状态转换的 MCU 校验交换。
- `com/gvs/vdp/talkback_is/GvsSdk_IS.smali`，约 489–504、2339–2354 行：注册 MCU 校验回调并启动流程。
- `GvsSdk_IS$2.smali` 和 `GvsSdk_IS$3.smali`：成功分支记录 `SDK authorization success` 并继续系统服务初始化；失败分支记录设备校验失败、按计数重试，超过阈值进入延迟失败处理。

发现：这不只是普通数据加密工具，确实为原 SDK 的设备校验提供运算。名称中的 Online 或 authorization 本身不能证明访问互联网；目前直接证据指向本机 MCU 交互。仍未完成整个应用所有授权路径的排除性检查。

`teaDecrypt` 虽有 native 导出，但在已检索的解包 smali 中未找到对应 Java 声明或直接调用。包装类的父类 `com/gvs/general/encrypt/a.smali` 也没有该声明。不能因此断言它在任何版本或动态路径中均不可达。

## F3：对 Doorfast 的实际影响

1. **被动解析与离线状态机测试**：不需要加载此库，可以继续保持公共头字段透明保存、严格长度检查和有证据的状态转换。
2. **原 Android SDK 直接复用**：存在 ARM64 指令集、Android JNI/Bionic/liblog，以及 MCU/系统服务依赖；不能把 `.so` 拷贝到 x86_64 ImmortalWrt 就运行。
3. **干净的 x86 独立实现**：本机 MCU 校验是原 SDK 架构中的依赖，不自动成为独立实现必须复制的功能；但这一判断不能消除网络端可能要求的协议认证。
4. **主机模式完成度**：公共头封装只是一个环节。身份配置、上线与刷新、来电路由、应答/结束、媒体协商和实际设备互操作仍需分别验证。

## 后续证据任务

- 静态追踪 `RandomCode` / `EncryptionCode` 的接收侧消费者，区分仅存储与实际校验，不推导绕过办法。
- 追踪 SDK 成功初始化回调建立的业务模块，整理哪些为 Android/MCU 专属、哪些为独立协议行为。
- 用现有合法捕获的报文扩充只读解析回归，保留“离线测试通过”和“真实设备接受”之间的证据边界。

本次仅新增分析文档，未改变 Doorfast 运行行为，也未构建或验证新的 x86 APK。

## 接收字段追踪补充（2026-09-08）

范围延续本文开头：仅检查本地已解包 APK，不涉及设备交互。采用普通逆向报告结构（flavor=null）。

### E4：字段引用检索

来源：主样本解包目录全部 `*.smali`；content_hash=n/a（目录检索结果）。可在保留同一解包目录的本机复现：

```sh
rg -n 'getRandomCode|getEncryptionCode|setRandomCode|setEncryptionCode' "$MOOGREN_DECODED" --glob '*.smali'
rg -n 'randomCode|encryptionCode' "$MOOGREN_DECODED" --glob '*.smali'
```

观察：第一项只有两个 getter 定义、两个 setter 定义，以及解析器中两个 setter 调用，没有 getter 调用。第二项的字段名引用全部位于 `BaseCommonData.smali`，包括定义、getter/setter 和 `toString()`。后者通过 `Arrays.toString` 将两个数组加入文本表示，不进行验证。

### E5：解析之后的分发

来源：`smali_classes3/com/gvs/general/protocol/f.smali:775` 起；content_hash=n/a。复现：

```sh
sed -n '775,855p' "$MOOGREN_DECODED/smali_classes3/com/gvs/general/protocol/f.smali"
```

观察：遍历注册列表，比较注册项功能码与 `getFunctionCode()`，检查注册端口为 8300，取得业务对象的 Handler。创建 Message，将 BaseCommonData 放入 `obj`，随后 `sendMessage`。该段没有比较上述两个字段。

### F4：已检查的 Java 对象接收路径未见字段校验

- severity：n/a_re；status：candidate；confidence：medium。
- evidence_ids：E4、E5；location：BaseCommonData 与 UDPDataAnalyzer 的公共控制消息分支。
- 结论：可见代码表现为“读取并保存两个字段，再分发业务消息”，没有发现对这两个对象字段的显式验证消费者。
- 限制：命名引用检索不能排除原始字节上的独立处理、反射、动态加载代码或 native 路径；本轮也没有覆盖门口机固件。因此不能提升为“所有接收端均不校验”的结论，更不能据此推导可省略发送端认证。
- 项目影响：保持字段原样解析，不把字段存在当作身份已认证；静态证据不构成真实设备互操作验证。

### P1：公共控制消息的对象分发路径

path_type=callflow：解析器保存两个字段（E4）→ 按注册功能码与端口选择业务对象（E5）→ 通过 Handler 传递 BaseCommonData（E5）。F4 仅覆盖这条可见路径，业务语义和设备接受策略仍须分别举证。

本轮记录顺序：检索 getter/setter → 补查直接字段引用与日志表示 → 检查 Handler 分发 → 更新结论与限制。下一步优先检查原始接收缓冲区在进入对象解析前的处理，确认是否还有独立的校验层；不重复以字段名称检索代替完整数据流分析。

## 原始接收链补充（2026-09-08）

### E6：Socket 到解析器的传递

来源为相同解包目录；content_hash=n/a（本地静态文本观察）。复现命令：

```sh
sed -n '38,220p' "$MOOGREN_DECODED/smali_classes3/com/gvs/general/udp/c\$1.smali"
sed -n '445,540p' "$MOOGREN_DECODED/smali_classes3/com/gvs/general/udp/c.smali"
sed -n '1,240p' "$MOOGREN_DECODED/smali_classes3/com/gvs/general/protocol/f\$1.smali"
sed -n '138,225p' "$MOOGREN_DECODED/smali_classes3/com/gvs/general/protocol/f.smali"
```

观察：

- `UDPConnect` 的 `c$1.run()` 每次循环创建缓冲区和 DatagramPacket，调用 `DatagramSocket.receive`，随后转交 `c.b(c, packet)`。
- 该 synthetic 方法直接调用 `c.a(DatagramPacket)`，将原数据包置于 Message.obj，设置消息类型，交给注册 Handler；未变换报文字节。
- `UDPDataAnalyzer` 的 `f$1.handleMessage` 经 synthetic 包装调用解析方法，没有中间认证运算。
- 解析方法按 `DatagramPacket.getLength()` 复制实际数据，检查至少 10 字节，然后比较公共固定头；通过后提取逻辑地址并按消息类型进入后续分支。未见此段调用 libencrypt 或对两个 8 字节字段做运算。

### F5 / P2：已检查的入口链未见独立密码校验层

F5：severity=n/a_re；status=candidate；confidence=medium；evidence_ids=E6（结合 E4、E5）；location=UDPConnect → UDPDataAnalyzer。

P2（path_type=callflow）：Socket.receive → 原 DatagramPacket 的 Message 分发 → Analyzer Handler → 实际长度复制与固定头检查 → 字段解析 → 业务 Handler。入口步骤对应 E6，末尾分发对应 E5。

边界：这将先前“解析器之前是否还有检查”的不确定性缩小到已检查链路之外；不是对操作系统、动态插桩、其他接收路径、其他设备固件或整个应用的排除性证明。固定头匹配是格式检查，不等价于发送者身份认证。不能据此声称任意发送报文会被门口机接受。

另见入口仅保证 10 字节存在，后续还访问完整公共头字段。因此不能从旧入口检查推出其已严格验证完整头长或声明载荷长度。Doorfast 应保持完整 42 字节头和载荷长度约束，而非为了复刻旧行为放松边界。未构造异常报文或进行动态漏洞验证。

本轮仅更新静态证据，无运行代码改动。接下来更有价值的是整理业务层的目标地址、来源及状态约束，而非继续将“没有发现 libencrypt 接收调用”推断为完整主机模式可用。

## 通话业务路由补充（2026-09-08）

### E7：状态先于操作码分支

`TalkBackBusiness.smali:10732` 的 `messageDeal` 在同步锁内读取当前状态，再选择不同消息处理方法。状态 0 对应 `a(Message)`（日志 NormalStandByDeal），状态 1 对应 `b(Message)`（WaitCallReplyDeal）；其余状态也分别分派，并非一个全局操作码表处理全部消息。

### E8：待机来电与来源绑定

`TalkBackBusiness.smali:652` 的待机处理，在操作码 1 分支先检查 talkbackEnableFlag，开启后将目标地址与本机地址 `d.o` 比较前 5 字节，匹配才进入来电处理。比较函数 `general/b/b.smali:331` 已检查，是指定偏移和长度的逐字节相等比较，不是加密验证。

`TalkBackBusiness.smali:1763` 的来电处理根据当前状态分支；待机路径保存完整 6 字节来源地址到 `u`、保存来源 IP，随后设置状态 6 并发布 CALL_IN 事件。非待机的部分分支会将完整来源与现有 `u` 或待处理的 `H` 比较；不能将所有后续操作概括成同一个来源匹配规则。

### F6 / P3：房间路由与会话关联应分层

F6：severity=n/a_re；status=candidate；confidence=medium；evidence_ids=E7、E8。可见逻辑包含功能开关、状态分派、房间级目标匹配与后续完整来源关联。这些是业务条件，不等同于密码学身份认证。

P3（callflow）：业务 Handler → 当前状态分支（E7）→ 待机功能开关与前 5 字节目标匹配（E8）→ 保存完整来源与 IP → 状态转换及 CALL_IN（E8）。该路径说明来电识别不是仅凭一个操作码；未证明独立主机的上线或发送报文被设备接受。

Doorfast 对照：`src/gvs_identity.c` 的 `df_gvs_frame_is_for_identity` 已比较前 5 字节；`src/gvs_session.c` 的摘机应答关联使用完整地址及会话时限。因此本轮无需把所有地址比较统一改成 5 字节或 6 字节。其余旧 SDK 状态分支尚未逐一完成对照，不宣称全面等价。

复现（仅本地静态读取；content_hash=n/a）：

```sh
sed -n '10732,10855p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
sed -n '652,935p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
sed -n '1763,2091p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
sed -n '331,391p' "$MOOGREN_DECODED/smali_classes3/com/gvs/general/b/b.smali"
```

记录顺序：检查业务总入口 → 待机分支 → 比较辅助函数 → 来源保存与事件发布 → 对照 Doorfast。此次仅更新报告，不修改逻辑或发送控制报文。下一项可限定为振铃期间重复来电、其他来源来电与结束消息的状态对照，形成离线回归依据。

## 振铃期间的行为差异（2026-09-08）

### E9：状态 6 的来电分支

来源：同一解包目录 `TalkBackBusiness.smali:4730–5194`，由已检查的状态分派表进入 `d(Message)`。

- 同一来源重复来电：完整 6 字节来源等于已保存的 `u` 时，只调用回复方法，然后退出本次分支；没有重新进入来电初始化或重设状态的调用。
- 不同来源来电：先由 `a([B)I` 分类，再由 `a(II)I` 比较新旧类型。高优先级分支记录 `high priority call in`，以 `HandUp Interrupt` 调用结束处理，然后进入新来电处理；其余分支记录低优先级并调用拒绝回复方法。
- 此处没有建立无限来电队列，不能从其他状态的待处理来源 `H` 推断本状态也有相同行为。

### E10：结束分支

同一文件的状态 6 操作码 2 分支先比较完整来源与 `u`，匹配才进入 `f([B[BLjava/lang/String;I)V`。该处理位于约 6610 行起，按状态停止相应业务，依据载荷首字节决定是否回复，清理参与者列表、回到状态 0，并发布 HAND_UP。载荷首字节不是这一段是否结束会话的总开关。

### F7：历史缺口：振铃抢占策略（现已实现离线版本）

severity=n/a_re；status=candidate；confidence=medium；evidence_ids=E9、E10；location=TalkBackBusiness 状态 6、Doorfast `src/gvs_session.c`。

| 情况 | 旧 SDK 已见行为 | Doorfast 当前行为 |
|---|---|---|
| 同来源重复来电 | 回复，不重复初始化 | 保持会话 generation；没有网络回复 |
| 其他来源来电 | 分类比较优先级，可能中断当前来电 | 已按已确认类别矩阵决定保持或抢占；不发送网络回复 |
| 匹配来源的结束消息 | 业务清理、状态归零、事件及条件回复 | 离线回放更新为 ENDED、清除待确认摘机；没有网络回复 |

P4（callflow）：振铃状态 → 操作码分支 → 来源关联 → 重传回复 / 优先级选择 / 结束处理，分别对应 E9、E10。这里的类型分类和地址比较不证明来源身份可信。

可复现静态读取（content_hash=n/a）：

```sh
sed -n '4730,5195p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
sed -n '483,635p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
sed -n '6610,6800p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
```

后续已将抢占整理为独立离线测试规格并实现：重传不换代、低优先级不替换、高优先级先结束旧会话再启动新会话。该实现仍没有设备行为验证，也没有加入发送端回复。

## 抢占清理与离线验收依据（2026-09-08）

### E11：分类比较与清理

来源：同一 `TalkBackBusiness.smali`。`a(II)I` 位于 492 行起，对有效类别 0、1、7、3、4 的结果显示：0/1/7 互不抢占，均可抢占 3 和 4；3 可抢占 4；同类不抢占。该表仅是 SDK 内部类别关系，不应将数字直接映射为未经核实的产品名称。未知类别不纳入此有效域结论。

抢占调用 `b(true, "HandUp Interrupt")`。其状态 6 路径先调用 `b(false,false)`：关闭活动标志，取消 `timer_hand_ask` 和 `timer_talkback_countdown`，通知 `stopRing`，调用 `stopVideoSend`。随后结束方法调用旧会话的协议发送辅助方法、置状态 0、清理参与者列表，发布包含旧来源和旧统计的 HAND_UP，并在有回调时输出 callRecord。返回后才进入新来电初始化。

可复现本地读取（content_hash=n/a）：

```sh
sed -n '492,634p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
sed -n '2867,3243p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
sed -n '10420,10500p' "$MOOGREN_DECODED/smali_classes3/com/gvs/vdp/talkback_is/TalkBackBusiness.smali"
```

### F8 / P5：抢占需完整结束旧会话，而非只换 peer

F8：severity=n/a_re；status=candidate；confidence=medium；evidence_ids=E9、E11。P5（callflow）：优先级判断 → 停止旧振铃及定时业务 → 旧会话清理和结束事件 → 新会话初始化。未执行动态测试；媒体底层释放和外部回调副作用不在本轮完整覆盖范围内。

### 离线测试规格（已实现并运行，未做设备验证）

| 场景 | 应观察到的结果 | 依据或限制 |
|---|---|---|
| 同一 peer 重复来电 | 不重复 CALL_IN，不重建会话，不延长原观察窗口 | E9；窗口不延长属于 Doorfast 本地策略 |
| 同级或低优先级的其他 peer | 保持旧 peer、旧 generation 和旧计时 | E9、E11；不发送实际忙线报文 |
| 高优先级的其他 peer | 旧结束事件先于新来电事件；新会话换代，清除旧摘机等待 | E11；generation 为 Doorfast 内部机制 |
| 抢占后旧 peer 的结束/摘机回复 | 不结束或接通新会话 | 完整来源关联与本地会话隔离要求 |
| 抢占后旧会话的本地定时回调 | 不改变新会话状态 | 需要本地 generation 绑定；不能仅取消定时器后假定无迟到回调 |
| 未知类别 | 保留为未支持，不自动给予更高优先级 | 防御性独立实现策略，非声称复刻旧版默认分支 |
| 同一 peer 结束后再次来电，旧报文迟到 | 明确记录歧义，不能凭本地 generation 宣称已区分 | 线上报文没有自动携带 Doorfast 的 generation |

最后一项修正此前测试设想的适用范围：“旧来源不能结束新会话”仅在来源不同或有额外可靠关联证据时成立。本地 generation 可以隔离定时器和内部任务，但不是已证实的线上会话标识。现有回放代码没有足够证据区分同一来源的旧结束报文与新结束报文。

后续实现位于 `src/gvs_priority.c`、`src/gvs_session.c` 和 `src/gvs_observer.c`，相应 C 与 Python 离线模型测试已加入仓库。真实设备的优先级回复及媒体清理仍未验证。
