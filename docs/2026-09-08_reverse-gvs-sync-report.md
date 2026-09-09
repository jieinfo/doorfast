# GVS 周期同步与版本维护逆向分析报告

> 分析日期：2026-09-08
> 报告类型：普通协议逆向，`flavor = null`
> 验证方式：APK 静态调用链、Doorfast C 单元测试和内存回放

## 1. 执行摘要

本阶段确认并实现了 GVS 室内终端在选举完成后的同步数据链路。同步数据使用 `91/03` 控制帧，载荷由两字节小端版本和 US-ASCII JSON 组成；全量周期数据以 20 项为一组分片，单字段变化使用 `Normal` 类型。Doorfast 已能在内存中登记项目维护的同步字段、更新版本、构造两类 JSON、解析自身兼容格式的入站同步帧，并按旧 APK 的版本和分机号规则选择接受或重发。首批两个静态确认字段已经进入项目适配器目录，但作为敏感字段默认禁用且拒绝空值启用。运行时新增脱敏状态快照与 JSON 查询，为后续 ubus/LuCI 展示同步阶段、维护角色和在线候选数量建立边界。`r6` 为 `07/81` 待回复对象增加固定容量、精确去重和一秒失效的离线队列；`r7` 完成有界单事务状态机；`r8` 将事务接入完整 48 字节 `07/81` 内存构帧、生产解析器回读及原子记录，生产运行时仍不发送网络报文。真实公共头认证、UDP 发送及设备接受性仍待独立验证。

## 2. 范围与目标

授权和网络边界见 [gvs-sync-scope.md](gvs-sync-scope.md)。本次目标是回答三件事：周期 JSON 的准确布局、同步版本如何变化、选举结束后维护者如何持续发送和故障接管。

| 属性 | 值 |
|---|---|
| 静态目标 | MT8157 APK 解包后的 `IndoorSyncBusiness` 与 `GVS_Protocol` smali |
| 实现目标 | `src/gvs_sync.c`、`src/gvs_presence.c` |
| 网络活动 | 无 |
| 公共头 | 复用 Doorfast 42 字节解析器和显式字段提供器 |

## 3. 协议与状态结论

`91/03` 的载荷布局如下，长度字段仍位于 42 字节公共头的偏移 40–41：

```text
VV VV 7B 22 54 59 50 45 ...
└版本┘ └────── US-ASCII JSON ──────┘
```

Doorfast 生成的规范化 JSON 为：

```json
{"TYPE":"Period","COUNT":2,"INFO":[{"KEY":"mode","VALUE":"home"},{"KEY":"scene","VALUE":"away"}]}
```

| 类型 | 项数 | 用途 | 版本行为 |
|---|---:|---|---|
| `Normal` | 固定 1 | 单字段即时同步 | 接收方采用报文版本并退出维护角色 |
| `Period` | 每帧 0–20 | 60 秒全量同步分片 | 按本机版本、对方版本和第六地址字节仲裁 |

本地字段每次调用更新接口都会递增版本，包括值相同的更新。版本小于 60000 时加一；当前版本达到或超过 60000 时下一版本为 1。字段必须先由 Doorfast 自身适配器登记，接收端会忽略未登记键。

周期仲裁复现旧 APK 的分支：本机版本较高时要求重发本机全量数据；版本相同且本机仍为维护者时，地址第六字节较小者优先；本机维护者收到更高版本时采用对方版本并让位。旧 APK 的非维护者收到更高版本 `Period` 时没有写回本地版本，Doorfast保留了这一行为并在测试中固定下来。`Normal` 更新会直接采用对方版本。

非维护者收到有效 `Period` 会把失联计数清零，并从接收时刻重新安排 60 秒截止时间。连续两个截止时间都没有收到周期同步时，本机成为维护者，并立即生成发往同户室内终端的 `PERIODIC_SYNC` 动作。

## 4. 实现与验证

`gvs_sync` 数据层提供固定容量登记表，避免运行时无界分配。它支持 JSON 字符串转义、1500 字节旧发送缓冲上限、20 项分片、`Normal`/`Period` 构帧，以及严格的入站结构解析。入站更新先作用于临时副本；完整验证成功后才提交数据和在线状态。

`gvs_runtime_sync` 已接入抓包循环：完整公共头中的 `0x91` 功能族由同步路由器处理，其他帧继续进入原通话接收器。当前动作回调只输出 `mode=passive` 日志；`resend_local` 也只记录决策。抓包中断时同步计时停止，恢复后保留版本和项目登记表重新进入上线阶段。

`gvs_sync_adapters` 登记首批两个从 MiniOS 静态确认的字段：`sync_mini1_secretkey` 和 `sync_mini2_secretkey`。它们均标记为敏感、默认禁用；未显式提供非空值时不会进入 `gvs_sync_store`，因此不会进入周期 JSON。状态接口只返回登记数和启用数，不返回键名和值。`df_gvs_runtime_sync_status` 提供结构化快照，目标构建通过只读 `doorfast.status` ubus 方法提供类型稳定的脱敏结果；独立 `luci-app-doorfast` 每 5 秒读取一次，并区分首次不可用与已有结果陈旧。ACL 只允许读取该方法，不包含配置或控制权限。

版本保存在 UCI 兼容的 `/etc/config/doorfast-sync`，其路径由主 UCI 配置的 `sync_state_path` 指定并限制为 `/etc/config/doorfast-*`。保存使用权限 `0600` 的同目录临时文件、`fsync` 和原子重命名；主配置不会被守护进程重写。

运行以下命令可复现本阶段验证：

```sh
cd /Users/shenwenjie/Documents/PVE/doorfast/.worktrees/feature-transparent-foundation
make -B test doorfast
sh tests/test_main_cli.sh
sh tests/test_gvs_peer_sim_cli.sh
sh tests/test_package_manifest.sh
node tests/test_luci_status.js
python3 -B -m unittest discover -s tests -p 'test_*.py'
```

另使用 AddressSanitizer 和 UndefinedBehaviorSanitizer 运行同一套 79 项 C 测试。本地编译、内存回放、确定性场景命令行测试、APK 软件包清单测试、LuCI JavaScript 和 Python 协议模型测试均纳入收尾验证。

## 5. Evidence → Finding → Path

### Evidence

#### E-001

- `title`: 旧 APK 构造周期和单字段 JSON，并以 20 项分片
- `observed_at`: 2026-09-08
- `source_type`: file
- `source_ref`: `/Users/shenwenjie/Downloads/moogren/work/moorgen-control-apk/apktool_out/smali_classes3/com/gvs/vdp/talkback_is/indoor/IndoorSyncBusiness.smali`
- `content_hash`: `f5c0a15825f60562886fb00f2866454aaf6bf19f9b5e84fd4d7b13e4e29ee9e1`
- `repro_command`: `sed -n '230,626p;1300,1740p;2186,2345p' /Users/shenwenjie/Downloads/moogren/work/moorgen-control-apk/apktool_out/smali_classes3/com/gvs/vdp/talkback_is/indoor/IndoorSyncBusiness.smali`
- `raw_excerpt`: 方法 `a([B,String)` 按 20 项构造 `TYPE=Period`；`a([B,String,String,String)` 构造单项 `TYPE=Normal`；`updateSyncInfo` 在 60000 处回绕到 1。
- `linked_workitem`: M2
- `supersedes`: none

#### E-002

- `title`: 旧协议层确认 `91/03`、小端版本和 ASCII JSON
- `observed_at`: 2026-09-08
- `source_type`: file
- `source_ref`: `/Users/shenwenjie/Downloads/moogren/work/moorgen-control-apk/apktool_out/smali_classes3/com/gvs/general/protocol/c.smali`
- `content_hash`: `f00c40bf2e45511d6e55ed5903d2231877073d43a2d78f1485d306eeb8a20196`
- `repro_command`: `sed -n '2041,2135p' /Users/shenwenjie/Downloads/moogren/work/moorgen-control-apk/apktool_out/smali_classes3/com/gvs/general/protocol/c.smali`
- `raw_excerpt`: `sendSyncInfoAsk` 写入功能族 `0x91`、操作码 `0x03`，长度为 JSON 字节数加 2，随后写入版本低字节、高字节和 US-ASCII JSON。
- `linked_workitem`: M2
- `supersedes`: none

#### E-003

- `title`: Doorfast 离线实现和回放测试通过
- `observed_at`: 2026-09-08
- `source_type`: command
- `source_ref`: `src/gvs_sync.c`, `src/gvs_presence.c`, `tests/test_gvs_sync.c`
- `content_hash`: `gvs_sync.c=b065c9cb58b96aa3450388c42a08da3847fe29a8147736dc5211b6001994ce1c; gvs_presence.c=2c7c90b5125d9baf6b779c774f3ddc2081c0de027fc7bf5202554c33511bbeb4; test_gvs_sync.c=49bb14c4493f4871cbcd36068e483afcb299ad98c0340583aa41be513c17cdc9`
- `repro_command`: `make -B test doorfast`
- `raw_excerpt`: 50 项 C 测试通过；周期分片、JSON 转义、版本回绕、入站事务、维护者仲裁、两周期接管、运行时分流和状态文件往返均有断言。
- `linked_workitem`: M2
- `supersedes`: none

#### E-004

- `title`: 被动运行时分流同步帧并持久化版本
- `observed_at`: 2026-09-08
- `source_type`: command
- `source_ref`: `src/gvs_runtime_sync.c`, `src/gvs_sync_state.c`, `src/runtime_service.c`
- `content_hash`: `gvs_runtime_sync.c=dc1ebf9519aec08c388d9137b46c6dfc790b713cef1d3fb21dde58908748c1f1; gvs_sync_state.c=8cd5bc0ff2ee5bfe47f68e7bc714fd2f05202848870a6e1cc948503d3f267939; runtime_service.c=e6e155a6a5747c53e9e6f54af090f6793131f8c334057f872c6129b1d1d2d2d8`
- `repro_command`: `make -B test doorfast && sh tests/test_package_manifest.sh`
- `raw_excerpt`: 同步功能族与通话帧分流；版本变化写入独立 UCI 文件；被动动作只记录日志；软件包安装两个配置文件。
- `linked_workitem`: M2
- `supersedes`: none

#### E-005

- `title`: 首批敏感适配器和脱敏运行时查询完成离线测试
- `observed_at`: 2026-09-08
- `source_type`: command
- `source_ref`: `src/gvs_sync_adapters.c`, `src/gvs_runtime_sync.c`, `tests/test_gvs_sync_adapters.c`, `tests/test_gvs_runtime_sync.c`
- `content_hash`: `gvs_sync_adapters.c=ac9c1a75a59bd2cb68a8a91ead3c382c3c9fb20427b8a64fa4012324e6ed6751; gvs_runtime_sync.c=dc1ebf9519aec08c388d9137b46c6dfc790b713cef1d3fb21dde58908748c1f1; test_gvs_sync_adapters.c=56c24c6932f34c89b610cbe288054f6242204836c95de9a11b80a42e85c4ea10; test_gvs_runtime_sync.c=5362f42076e7455f289823acd607b3b8564ace0dcbc55fd5bc6d65918c05bc63`
- `repro_command`: `make -B test doorfast && sh tests/test_package_manifest.sh`
- `raw_excerpt`: 两个敏感字段默认不进入同步存储；空值启用被拒绝；状态 JSON 返回角色和计数，但不包含字段名或合成字段值。
- `linked_workitem`: M2, M5
- `supersedes`: none

#### E-006

- `title`: 只读 ubus 与 LuCI 状态边界通过主机测试和软件包清单验证
- `observed_at`: 2026-09-08
- `source_type`: command
- `source_ref`: `src/runtime_ubus.c`, `package/luci-app-doorfast`, `tests/test_runtime_ubus.c`, `tests/test_luci_status.js`
- `content_hash`: `runtime_ubus.c=de427722320235edd1db9a5385f44ceb120533821d586523b798e7d78e812520; runtime_service.c=19f2037339689b5fa9f8539cc3eed121403ee8cb4da2322f0c460d4b2c53ccc7; status_model.js=67add309df3af86bcb3a2bb7e0d18b5fd659b29ee2ea4d11aa0b8a329cdebdbf; status.js=46c2278316918696539326670f5d54a99c328afca1920134a1243907e42a2b2f; acl.json=ece9d8b785b78d66278b3ca6113f661158f775407e5e8ed19169a80cc9846f3b; test_runtime_ubus.c=8f80472c0048ad6cb1a4cad22da9f61e3daeb8cd3d3ca0e5caed5a0bcdcb6fd7; test_luci_status.js=628d46cbd9ee716d07024e4ccf5beb4064be281a969361d157c4de944c7de8f7; doorfast-0.1.0-r1.apk=da56d8649b9d0a7be17541c78b825586c4f766223c2a5c25212d78b67878bfe6; luci-app-doorfast-0.1.0-r1.apk=4a131ce902750d7333aa7144557e0b4c4ea85e64cc1f26e561472a82966b2430`
- `artifact_path`: `build/ci-34214119274/immortalwrt-sdk-25.12.1-x86-64_gcc-14.3.0_musl.Linux-x86_64/bin/packages/x86_64/base/`
- `repro_command`: `make -B test doorfast && sh tests/test_main_cli.sh && sh tests/test_package_manifest.sh && node tests/test_luci_status.js`
- `raw_excerpt`: 本机 56 项 C 测试、ASan/UBSan、CLI、包清单、LuCI JavaScript 和 8 项协议模型通过；GitHub Actions `34214119274` 在提交 `2be7a56d96bad62f5c4bc395b63dd891557dc697` 成功生成两个 ADB 格式 APK。Doorfast 提供只读 `status` 方法；LuCI 只展示脱敏状态并每 5 秒刷新；ACL 不含写入、服务或命令权限。目标系统安装运行尚未验收。
- `linked_workitem`: M5, P
- `supersedes`: none

#### E-007

- `title`: 官方 ImmortalWrt 25.12.1 x86_64 虚拟机完成 APK 运行验收
- `observed_at`: 2026-09-08
- `source_type`: command
- `source_ref`: `src/capture.c`, `src/runtime_service.c`, `package/doorfast`, `package/luci-app-doorfast`
- `content_hash`: `immortalwrt-25.12.1-x86-64-generic-ext4-combined.qcow2.gz=234f14e4e29282f887327fdd23695126fa41fef88829d792f95565d60b60fbc6; capture.c=367fd022c6e2e01ad048200ece067924e9559a3d82e97d5943b5f164e4f37581; runtime_service.c=dde36acd83e0e3e5430f5a93389d98ffcc6e1aa7c8dae56c5722fd81841a219e; doorfast-0.1.0-r1.apk=47fca4cd2961bf4b906bcfbe917d7cb92aacf084c5cc3f13c1967907b3b0376b; luci-app-doorfast-0.1.0-r1.apk=4a131ce902750d7333aa7144557e0b4c4ea85e64cc1f26e561472a82966b2430`
- `artifact_path`: `build/ci-34218272315/immortalwrt-sdk-25.12.1-x86-64_gcc-14.3.0_musl.Linux-x86_64/bin/packages/x86_64/base/`
- `repro_command`: `install-apks.sh ARTIFACT_DIRECTORY && smoke-test.sh`; `reboot` 后再次运行 `smoke-test.sh`；`apk del luci-app-doorfast doorfast` 后重新安装并运行 `smoke-test.sh`
- `raw_excerpt`: QEMU 11.1.1 TCG 上的 ImmortalWrt 25.12.1 `r37978-cd0a06bfd3fd`、`x86_64`、musl 环境成功安装两个 APK 及 `libpcap1` 等依赖。目标二进制动态链接到 x86_64 musl、libpcap、libubus、libubox、libblobmsg-json 和 libjson-c。空闲抓包时 `ubus -t 2 call doorfast status '{}'` 可返回；向 `br-lan` 注入一条目标身份匹配、严格 42 字节公共头的合成 UDP/8300 来电帧后记录 `event=IncomingCall generation=1`，证明目标机捕获、提取、解析和会话入口贯通。procd 停止后进程和 ubus 对象立即消失；启动及整机重启后单实例自动恢复。LuCI JavaScript 资源返回 HTTP 200，管理路径要求登录。卸载移除二进制和运行态同步文件，保留用户修改过且 SHA-256 不变的核心 UCI 配置；重装后同步文件重新生成。Actions `34218272315` 对提交 `ad2e80ed6a65629efc3c470f8273c7cd117b6025` 构建成功。开发包签名未加入干净系统信任库，测试时使用 `--allow-untrusted`；正式发布签名尚未验收。
- `linked_workitem`: M1, M5, P
- `supersedes`: none

#### E-008

- `title`: `r3` APK 通过自动升级重启和备份恢复式回滚验收
- `observed_at`: 2026-09-08
- `source_type`: command
- `source_ref`: `package/doorfast/Makefile`, `tests/test_package_manifest.sh`, GitHub Actions `34243314340`, ImmortalWrt 25.12.1 x86_64 虚拟机
- `content_hash`: `package/doorfast/Makefile=4a3223714d03ea2b8cc8240abde39a0de467c1e05276883bee6afd5241c33a9c; tests/test_package_manifest.sh=8e61e71f32296ba98255ea057df307cdd6f19467677a83bc837acb30ab373609; doorfast-0.1.0-r3.apk=7b3f26d1df5e31f6656602dc6d49ec3895a92f6804658ded7d9198542d4f2c03; luci-app-doorfast-0.1.0-r1.apk=4a131ce902750d7333aa7144557e0b4c4ea85e64cc1f26e561472a82966b2430`
- `artifact_path`: `build/ci-34243314340/doorfast-apk/immortalwrt-sdk-25.12.1-x86-64_gcc-14.3.0_musl.Linux-x86_64/bin/packages/x86_64/base/`
- `repro_command`: `sh tests/test_package_manifest.sh`; 在目标虚拟机依次安装 `r2`、记录 PID/配置哈希、安装 `r3`；备份配置后移除 `r3`、安装 `r2`、恢复配置，再安装 `r3`
- `raw_excerpt`: Actions `34243314340` 对提交 `d287b21457156d55a8850b0aeec4f3929597db25` 构建成功。`r2 → r3` 时进程 PID 从 `2614` 变为 `3483`；回滚恢复后再次升级时 PID 从 `4033` 变为 `4160`。核心配置 SHA-256 始终为 `7743882baabc56f768df4dda131a74c9c22e79048087ab06caee265dc32f8e87`，同步状态 SHA-256 始终为 `f6f6b71114a17035edd30b06282c121d35711abbfe44bf8741e13bc6955f1e4d`，ubus 在两次升级后均返回 `version=321`。apk-tools 3 拒绝不存在的 `--allow-downgrade` 选项，因此回滚采用备份、移除、安装旧包、恢复和启动流程。修改后的同步配置在卸载时被保留；此前默认同步配置会被移除。
- `linked_workitem`: M1, M5, P
- `supersedes`: none

#### E-009

- `title`: 离线 GVS 对端模拟器贯通生产选举、接管与来电路由入口
- `observed_at`: 2026-09-09
- `source_type`: command
- `source_ref`: `tests/support/gvs_peer_sim.c`, `tools/gvs-peer-sim.c`, `tests/test_gvs_peer_sim.c`, `tests/test_gvs_peer_sim_cli.sh`
- `content_hash`: `gvs_peer_sim.c=8a2eccc1b20a1c39431726b3790d166cc3302686b1e414798c44d9408781a21d; gvs-peer-sim.c=c35fa55be90890af8a30fb700a2bbe6642e8f2337bf047445f3622a83f691e4d; test_gvs_peer_sim.c=cf398d45c2494d3ddf58c8c392f5fa8a66f6fab4d2c23677b07005d534f0c52b; test_gvs_peer_sim_cli.sh=dd8612b81b06af156e1ac5bf919b7b47bbfe3d69de65a4d18a297f5112bf2ef8`
- `artifact_path`: `tests/support/gvs_peer_sim.c`, `tools/gvs-peer-sim.c`, `tests/test_gvs_peer_sim.c`, `tests/test_gvs_peer_sim_cli.sh`
- `repro_command`: `make -B test doorfast peer-sim && sh tests/test_gvs_peer_sim_cli.sh && sh tests/test_package_manifest.sh`
- `raw_excerpt`: 无对端场景在 7500 ms 成为维护者，并产生 9 次同步询问和 9 次版本询问；低分机号对端使本机保持跟随者；维护者场景在一个有效周期后第一次漏周期仍跟随，第二次漏周期接管。同户目标来电进入 `ringing`，其他住户保持 `idle`。同步对端始终为 `online_peers=0`，明确保留候选在线回复的证据缺口。重复运行 JSON Lines 逐字一致，且不输出固定公共头字段。
- `linked_workitem`: M2
- `supersedes`: none

#### E-010

- `title`: 隔离 ImmortalWrt x86_64 虚拟机贯通周期同步、版本持久化和失联接管
- `observed_at`: 2026-09-09
- `source_type`: network
- `source_ref`: `tools/gvs-peer-udp-inject.c`, `tests/test_gvs_peer_udp.py`, `tests/run_gvs_vm_udp.py`, `docs/gvs-vm-udp-validation.md`, ImmortalWrt 25.12.1 x86_64 虚拟机
- `content_hash`: `gvs-peer-udp-inject.c=77c339e7d37a1c674e1f4007423b5d550a44dc104bbcfb1cd7906837c75b1d8f; test_gvs_peer_udp.py=e1cd5e155ea3fe6864125ba4f3f4eea382f3a794d482c2ba984bdefa219f017f; run_gvs_vm_udp.py=a452d501c53d5476550d8b640a7fbc5ab744898fbc599f16b6f966f948cebcb3; gvs_peer_sim.c=a2e9f32917705a6f2ea29639e0d5e8e7c48a2af292f6124669240a0cc3c2b808; gvs_peer_sim.h=dbd3792eb22471a2f3dc9e50f88d36dbebde34fffcd2f42b22aea27f5acb1268`
- `artifact_path`: `tools/gvs-peer-udp-inject.c`, `tests/test_gvs_peer_udp.py`, `tests/run_gvs_vm_udp.py`, `tests/support/gvs_peer_sim.c`, `tests/support/gvs_peer_sim.h`
- `repro_command`: `python3 -B tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh --wait-for-takeover`
- `raw_excerpt`: QEMU 用户网络把固定回环端口 `127.0.0.1:18300` 转发至虚拟机 UDP/8300。运行于官方 ImmortalWrt 25.12.1 x86_64 的 `doorfast-0.1.0-r3.apk` 先在 `SYNC_ASK` 接受合成 `91/81` 并成为 follower；随后接受 `91/03 Period` 版本 7 和 `91/03 Normal` 版本 8，ubus 显示 `last_opcode=3, last_accepted=true`，UCI `doorfast-sync.sync.version` 写入 8。本户 `03/01` 产生新的 `IncomingCall`。停止周期输入后，第一次 60 秒截止观测为 `role=follower, periodic_misses=1`，第二次截止观测为 `role=maintainer, periodic_misses=0`，并新增被动 `periodic_sync` 动作日志。注入器只接受四种内置场景且目的地址固定；APK 包清单排除 `gvs-peer-` 测试工具。
- `linked_workitem`: M1, M2
- `supersedes`: none

#### E-011

- `title`: `07/81` 候选在线语义完成静态与 x86_64 运行双重验证
- `observed_at`: 2026-09-09
- `source_type`: file, network
- `source_ref`: Moorgen APK `ManagerBusiness`/`IndoorDeviceBusiness`/`GVS_Protocol` smali，Doorfast `src/gvs_presence.c`、`src/runtime_service.c`、`tests/run_gvs_vm_udp.py`，ImmortalWrt 25.12.1 x86_64 虚拟机
- `content_hash`: `moorgen_apk=6793c5777bea2c4f56f30c79c19d37d9089976eaaaa61c6da0ef6724a9a8487f; doorfast-0.1.0-r4.apk=518ed2bb05121250112b84b5214af47ab663db64feb7442308c65da2bb2517d4; gvs_presence.c=cf8ea2c23d8d5bdeb984dadc8b0f2023c2fbbe0ce522a7045424e6c83e86eb53; runtime_service.c=f276eca315cb765fd73258b5aef9cbb87db095e46dcd52f5aaac5d211a5b27d8`
- `artifact_path`: `docs/gvs-peer-online-reply.md`, `src/gvs_presence.c`, `src/runtime_service.c`, `tests/run_gvs_vm_udp.py`
- `repro_command`: `python3 -B tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh --wait-for-takeover`
- `raw_excerpt`: 旧 APK 将功能码 7 注册到 ManagerBusiness；`0x81` 分支以源逻辑地址刷新候选设备，倒计时重置为 60 秒，线程每秒递减并在 30 秒倍数探测。Doorfast `r4` 在隔离虚拟机收到固定 48 字节 `07/81` 后状态为 `online_peers=1`，60 秒后为 0；随后同步版本保持 8，第一次周期缺失仍为 follower，第二次转为 maintainer，本户来电记录 `IncomingCall`。
- `linked_workitem`: M2
- `supersedes`: none

#### E-012

- `title`: 旧 APK 的 `07/01` 处理顺序与 `07/81` 载荷来源完成静态确认
- `observed_at`: 2026-09-09
- `source_type`: file
- `source_ref`: Moorgen APK `ManagerBusiness.smali:682-732`、`GVS_Protocol c.smali:2214-2385`
- `content_hash`: `moorgen_apk=6793c5777bea2c4f56f30c79c19d37d9089976eaaaa61c6da0ef6724a9a8487f`
- `artifact_path`: `docs/gvs-peer-online-reply.md`
- `repro_command`: `sed -n '660,742p' /absolute/path/to/decoded/smali_classes3/com/gvs/vdp/talkback_is/manager/ManagerBusiness.smali && sed -n '2214,2385p' /absolute/path/to/decoded/smali_classes3/com/gvs/general/protocol/c.smali`
- `raw_excerpt`: `COM_PING_ASK` 分支先以请求源地址、目标 IP/端口、请求数据和空 MAC 参数调用回复方法，再尝试以源地址刷新候选在线状态。空 MAC 分支把请求数据前两字节写入 `07/81` 的 6 字节载荷，其余四字节写零。
- `linked_workitem`: M2
- `supersedes`: none

#### E-013

- `title`: Doorfast 离线接收 `07/01` 并生成待处理 `07/81` 回复
- `observed_at`: 2026-09-09
- `source_type`: command
- `source_ref`: `src/gvs_presence.c`, `src/gvs_serialize.c`, `src/runtime_service.c`, `tests/test_gvs_presence.c`, `tests/test_gvs_serialize.c`
- `content_hash`: `gvs_presence.c=41f4e889df33ebdd13f9045b995bab2b626fe9c50baf4ef7abcefb0101f0eb10; gvs_serialize.c=17eb4dc8c895faf64b6926ab72e7afb136adb21fa71d62975d3c21c639fd9531; runtime_service.c=df38250f986358635f9e0b93a75751317b4204e0283fdffb13afd0bc64e042f3; test_gvs_presence.c=45baceb2f01a55a22243129261b0698dc80c8d547c5f3f0eab02f9bbdeb33954; test_gvs_serialize.c=95cdbdc5aff2b31542c48ccddaf4890e691b3b86f6d691b5144299d3ec27b7cc`
- `artifact_path`: `src/gvs_presence.c`, `src/gvs_serialize.c`, `src/runtime_service.c`, `tests/test_gvs_presence.c`, `tests/test_gvs_serialize.c`
- `repro_command`: `make clean && make test && make clean && make CFLAGS='-std=c17 -Wall -Wextra -Werror -pedantic -Isrc -Itests -Itests/support -I/opt/homebrew/Cellar/libpcap/1.10.6/include -fsanitize=address,undefined -fno-omit-frame-pointer' test`
- `raw_excerpt`: 66 项 C 测试通过；有效 `07/01` 生成目标为请求源的待回复对象，已知候选刷新 60 秒在线期限，未知来源不进入候选状态；序列化结果为 48 字节 `07/81`，载荷为请求前两字节加四个零。截断、多余数据、错误目标/功能码/操作码/长度及事件交付失败均被拒绝且不修改状态。生产运行时仅记录 `reply_pending=1`，没有发送调用。
- `linked_workitem`: M2
- `supersedes`: none

#### E-014

- `title`: r5 APK 完成目标虚拟机探测请求接收验收
- `observed_at`: 2026-09-09
- `source_type`: log
- `source_ref`: GitHub Actions 34299375775，ImmortalWrt 25.12.1 x86/64 QEMU，tests/run_gvs_vm_udp.py
- `content_hash`: doorfast-0.1.0-r5.apk=fbc33c632d39db09c39308028229b93ecf7a6b5b5e5926d5d954f30b980e2c00; installed_daemon=00e4bfaee53cc88d771a1c11bd33889f30b8043ed3798df47215ef3b376ba738
- `artifact_path`: build/ci-34299375775/immortalwrt-sdk-25.12.1-x86-64_gcc-14.3.0_musl.Linux-x86_64/bin/packages/x86_64/base/doorfast-0.1.0-r5.apk
- `repro_command`: `python3 -B tests/run_gvs_vm_udp.py /Users/shenwenjie/Documents/PVE/vms/doorfast-immortalwrt-25.12.1-x86_64/ssh.sh`
- `raw_excerpt`: r4 升级至 r5 成功；实际程序为 x86-64 musl ELF。日志出现 peer_probe accepted=1 reply_pending=1 peer_observed=1 mode=passive、peer_reply accepted=1 和 IncomingCall generation=1；ubus 为 follower、online_peers=1、version=8，UCI 版本为 8。短流程和服务冒烟通过。本轮未重跑两个 60 秒周期，未进行独立出站抓包；零发送结论来自当前源码没有发送路径。
- `linked_workitem`: M2
- `supersedes`: none

#### E-015

- `title`: Doorfast 建立固定容量的离线探测回复队列
- `observed_at`: 2026-09-09
- `source_type`: command
- `source_ref`: `src/gvs_reply_queue.c`, `src/runtime_service.c`, `tests/test_gvs_reply_queue.c`, `tests/run_gvs_vm_udp.py`
- `content_hash`: `gvs_reply_queue.c=f643982c18e736a4728607468ed488bde51a63d65ac33df05884ecb6051d3dc4; gvs_reply_queue.h=eedd0ed92f2047d447615ca10079210e913332d90bbb693207cf8e705465868f; runtime_service.c=b7252dc607bf73d84d0799484708b64244849396478be6b45d7952cb11470ee7; test_gvs_reply_queue.c=4eff9131925472f05a1dd27d336c96cf34bde1e832b7974f636308aade7cef46; run_gvs_vm_udp.py=f48edaceb8179fc3f54ca6054a52afbb41b3986369c798962aab92c88b1f70d8`
- `artifact_path`: `src/gvs_reply_queue.c`, `src/gvs_reply_queue.h`, `src/runtime_service.c`, `tests/test_gvs_reply_queue.c`, `tests/run_gvs_vm_udp.py`, `docs/gvs-reply-queue.md`
- `repro_command`: `make clean && make test doorfast peer-sim peer-udp-inject && python3 -B -m unittest tests/test_gvs_peer_udp.py && sh tests/test_gvs_peer_sim_cli.sh && sh tests/test_package_manifest.sh`
- `raw_excerpt`: 68 项 C 测试和 Sanitizer 通过。队列固定 16 项、有效期 1000 毫秒；相同目标和请求数据合并并刷新期限，不同请求独立。过期清理、FIFO 取出、满队列、时间回退和溢出路径均有测试；运行时只入队、过期和记录脱敏日志，不调用取出或发送。
- `linked_workitem`: M2
- `supersedes`: none

#### E-016

- `title`: Doorfast 建立有界的离线发送事务状态机
- `observed_at`: 2026-09-09
- `source_type`: command
- `source_ref`: `src/gvs_send_transaction.c`, `src/runtime_service.c`, `tests/test_gvs_send_transaction.c`, `tests/run_gvs_vm_udp.py`
- `content_hash`: `cb15399d8260f25e900db665c40953fe71be08fbc250619b24ea96e6febd9ca8`
- `artifact_path`: `src/gvs_send_transaction.c`
- `repro_command`: `make clean && make test doorfast peer-sim peer-udp-inject && python3 -B -m unittest tests/test_gvs_peer_udp.py && sh tests/test_gvs_peer_sim_cli.sh && sh tests/test_package_manifest.sh && sh tests/test_main_cli.sh && node tests/test_luci_status.js`
- `raw_excerpt`: 75 项 C 测试及 AddressSanitizer/UndefinedBehaviorSanitizer 通过。单事务状态机从 FIFO 队列取出待回复对象，最多尝试 3 次；单次期限 250 毫秒，失败或超时后等待 100 毫秒重试。即时成功、异步失败后成功、三次失败、三次无响应最终超时、跨对象迟到完成、双时钟一致性和接近时钟上限的终态均有离线测试；守护进程只使用 `mode=simulated` 的即时成功发送器。
- `linked_workitem`: M2
- `supersedes`: none

#### E-017

- `title`: r7 APK 在目标虚拟机贯通抓包入口到模拟发送终态
- `observed_at`: 2026-09-09
- `source_type`: command
- `source_ref`: GitHub Actions `34311582809`, `tests/run_gvs_vm_udp.py`, ImmortalWrt 25.12.1 x86_64 虚拟机
- `content_hash`: `doorfast-0.1.0-r7.apk=701e4e8d6bbda83d80bd236af0d1dae0ee77449f5e2eb2b68aa225a120139a5e; /usr/sbin/doorfast=26e61740f5e80f3972fe90403f84cd634290cd2e0fe948dc104ad7b630874b3c`
- `artifact_path`: GitHub Actions artifact `doorfast-apk`, installed `/usr/sbin/doorfast`, `docs/gvs-vm-udp-validation.md`
- `repro_command`: `python3 -B tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh`
- `raw_excerpt`: `doorfast-0.1.0-r7` 在官方 ImmortalWrt 25.12.1 x86/64 隔离虚拟机安装并由 procd 运行。两个固定 `07/01` 探针分别产生一组 `peer_reply_tx state=sending` 和 `state=success attempt=1 timed_out=0 mode=simulated`；随后 `07/81` 保持 `online_peers=1`，Normal 同步将运行时和 UCI 版本更新至 8，本户 `03/01` 产生新的 IncomingCall。短流程通过，未向真实网络发送报文。
- `linked_workitem`: M2
- `supersedes`: none

#### E-018

- `title`: Doorfast 以可替换公共头提供器构造并记录完整 `07/81` 内存帧
- `observed_at`: 2026-09-09
- `source_type`: command
- `source_ref`: `src/gvs_memory_sender.c`, `src/runtime_service.c`, `tests/test_gvs_memory_sender.c`, `tests/run_gvs_vm_udp.py`
- `content_hash`: `gvs_memory_sender.c=5ebf4a66b781cdc08f7ea2e832ceb6b1cf953806d1d5c74cfb2722ea045250ef; gvs_memory_sender.h=a9c71ff00a8da57389a7f945711b779afc31f143fa790e8976a6b86db91f8ab4; test_gvs_memory_sender.c=2c901c4c9e364e21b2a2d1de93acc2ebbc8700287348527603581d122df1f89d; runtime_service.c=1359831dfbbaf43bb6e594827b95a8ea71a97ca5d2dda18f1d15bd5e8c42003c; run_gvs_vm_udp.py=4d290dbdd587cf9643724908fef0d1465917e095338ba91409ead81cbf357f34`
- `artifact_path`: `src/gvs_memory_sender.c`, `src/gvs_memory_sender.h`, `tests/test_gvs_memory_sender.c`, `src/runtime_service.c`, `tests/run_gvs_vm_udp.py`, `docs/gvs-inmemory-frame-adapter.md`
- `repro_command`: `make clean && make test doorfast peer-sim peer-udp-inject && python3 -B -m unittest tests/test_gvs_peer_udp.py && sh tests/test_gvs_peer_sim_cli.sh && sh tests/test_package_manifest.sh && sh tests/test_main_cli.sh && node tests/test_luci_status.js`
- `raw_excerpt`: 79 项 C 测试及 AddressSanitizer/UndefinedBehaviorSanitizer 通过。适配器为每个待回复对象生成 48 字节 `07/81`，以本机逻辑地址为源、请求源为目标、请求前两字节加四个零为载荷；生成后由生产解析器回读并核对地址、功能码、操作码、声明长度和载荷。构帧失败不覆盖旧记录并触发事务重试。运行时公共头的两个 8 字节字段为明确标记的全零占位值，只记录长度和尝试号，不发送网络报文。
- `linked_workitem`: M2
- `supersedes`: none

### Findings

#### F-001

- `title`: `91/03` 是选举后的同步数据帧
- `severity`: n/a_re
- `category`: reverse_algo
- `status`: validated
- `evidence_ids`: E-001, E-002, E-003
- `location`: `IndoorSyncBusiness.messageDeal`, `GVS_Protocol.sendSyncInfoAsk`, `src/gvs_sync.c`
- `impact`: Doorfast 可以在不依赖 Android JSON 组件的情况下生成并离线接收该数据格式。
- `confidence`: high
- `repro_steps`: 运行 E-001/E-002 的静态定位命令，再运行 E-003 测试。
- `remediation`: n/a

#### F-002

- `title`: 非维护者通过两个缺失周期完成故障接管
- `severity`: n/a_re
- `category`: reverse_algo
- `status`: validated
- `evidence_ids`: E-001, E-003
- `location`: `IndoorSyncBusiness$1.run`, `src/gvs_presence.c`
- `impact`: Doorfast 的离线状态机能够从选举进入持续同步，并在维护者消失后重新产生全量同步动作。
- `confidence`: high
- `repro_steps`: 运行 `test_gvs_presence_takes_over_after_two_missed_periods` 所在测试套件。
- `remediation`: n/a

#### F-003

- `title`: 同步接收与版本恢复已进入被动守护进程
- `severity`: n/a_re
- `category`: design
- `status`: validated
- `evidence_ids`: E-003, E-004
- `location`: `src/runtime_service.c`, `src/gvs_runtime_sync.c`, `src/gvs_sync_state.c`
- `impact`: 实际抓包循环可以维护同步角色和版本，同时保持零网络发送；服务重启和抓包恢复不会无条件丢失版本。
- `confidence`: high
- `repro_steps`: 运行 E-004 的构建和包清单命令。
- `remediation`: n/a

#### F-004

- `title`: 同步适配器与管理状态之间已建立脱敏边界
- `severity`: n/a_re
- `category`: design
- `status`: validated
- `evidence_ids`: E-001, E-005
- `location`: `src/gvs_sync_adapters.c`, `src/gvs_runtime_sync.c`
- `impact`: 后续 ubus/LuCI 可以展示同步阶段、维护角色和在线候选数量，而不会通过该接口暴露敏感同步字段。
- `confidence`: high
- `repro_steps`: 运行 E-005 命令并检查 `test_gvs_runtime_sync_exposes_redacted_status_snapshot`。
- `remediation`: n/a

#### F-005

- `title`: 脱敏同步状态已具有只读本地管理入口
- `severity`: n/a_re
- `category`: design
- `status`: validated
- `evidence_ids`: E-005, E-006
- `location`: `src/runtime_ubus.c`, `package/luci-app-doorfast`
- `impact`: 本地管理员可以观察在线维护阶段和角色，且界面不会暴露同步键值或获得门禁控制能力。
- `confidence`: high
- `repro_steps`: 运行 E-006 的主机测试与包清单验证；目标系统安装验收另行记录。
- `remediation`: n/a

#### F-006

- `title`: 被动服务 APK 已通过目标系统生命周期冒烟
- `severity`: n/a_re
- `category`: other
- `status`: validated
- `evidence_ids`: E-006, E-007
- `location`: `src/capture.c`, `src/runtime_service.c`, `package/doorfast`, `package/luci-app-doorfast`
- `impact`: Doorfast 核心和只读状态页可以在官方 ImmortalWrt 25.12.1 x86_64 环境安装并由 procd 持续运行；空闲抓包不会再阻塞 ubus，合成 GVS 来电帧可从目标网卡进入会话状态机。该结论仅覆盖被动平台生命周期和合成接收，不证明真实门口机互操作、主动上线或完整主机模式。
- `confidence`: high
- `repro_steps`: 使用 E-007 的官方镜像、APK 和命令复现首次安装、启停、冷启动及卸载重装。
- `remediation`: 正式发布前补签名信任、版本升级/降级、长期运行和真实网络流量验收。

#### F-007

- `title`: 核心 APK 升级会切换运行进程并保持配置状态
- `severity`: n/a_re
- `category`: other
- `status`: validated
- `evidence_ids`: E-007, E-008
- `location`: `package/doorfast/Makefile`, ImmortalWrt apk `post-upgrade`
- `impact`: 从 `r3` 开始，目标系统升级后无需管理员另行重启即可运行新二进制；主配置和同步版本跨升级保持不变。apk-tools 3 的旧版本恢复需要移除重装和显式备份恢复，不能宣传为原地降级。
- `confidence`: high
- `repro_steps`: 按 E-008 在目标虚拟机执行 `r2 → r3`，比较升级前后 PID、配置哈希和 ubus 版本；再执行备份恢复式回滚。
- `remediation`: 正式发布前签署两个包，并在未来配置结构变化时为每个迁移边界增加目标机测试。

#### F-008

- `title`: 本地协议对端可以确定性验证选举、失联接管和同户来电选择
- `severity`: n/a_re
- `category`: other
- `status`: validated
- `evidence_ids`: E-001, E-009
- `location`: `tests/support/gvs_peer_sim.c`, `tools/gvs-peer-sim.c`, `src/gvs_runtime_sync.c`, `src/gvs_receive.c`
- `impact`: Doorfast 的身份候选、同步状态机、完整 42 字节帧校验和会话入口可以在不联网的情况下端到端回归；这缩小了进入隔离 UDP 测试前的代码不确定性，但不证明真实门口机接受 Doorfast。
- `confidence`: high
- `repro_steps`: 运行 E-009 命令，比较两次 `no-peer` 输出，并检查三个内置场景的角色、漏周期和目标范围记录。
- `remediation`: `0x07` 请求/回复离线语义已经补齐；下一阶段验证目标 APK 的请求入口，并继续保留真实设备接受性缺口。

#### F-009

- `title`: 目标 APK 的被动 UDP 路径能够持续维护同步状态并完成失联接管
- `severity`: n/a_re
- `category`: other
- `status`: validated
- `evidence_ids`: E-003, E-010
- `location`: `src/runtime_service.c`, `src/gvs_runtime_sync.c`, `src/gvs_presence.c`, ImmortalWrt 25.12.1 x86_64 虚拟机
- `impact`: 合成 `91/81`、`91/03 Period` 和 `91/03 Normal` 已经过目标系统的真实网卡捕获、UDP 提取、运行时解析、版本持久化和两周期接管链路；后续可以把精力集中到在线回复语义、合法公共头提供器和真实门口机认可。
- `confidence`: high
- `repro_steps`: 使用 E-010 的固定回环转发和命令执行完整虚拟机验证，检查 follower 第一次缺失、maintainer 第二次接管、UCI 版本 8 及新增 `periodic_sync` 日志。
- `remediation`: 保持生产服务被动，直到合法公共头字段和真实设备接受性具备独立证据。

#### F-010

- `title`: 候选室内机在线应答可驱动 Doorfast 的在线维护状态
- `severity`: n/a_re
- `category`: reverse_algo
- `status`: validated
- `evidence_ids`: E-001, E-011
- `location`: `ManagerBusiness.messageDeal`, `IndoorDeviceBusiness`, `src/gvs_presence.c`, `src/runtime_service.c`
- `impact`: Doorfast 已能从实际 x86_64 抓包入口识别同户候选的 `07/81` 应答、刷新 60 秒期限并通过 ubus 暴露在线数量；候选消失后会自动回到离线。
- `confidence`: high
- `repro_steps`: 运行 E-011 命令，观察固定应答后 `online_peers=1`、60 秒后为 0，并核对 `peer_reply accepted=1` 与 `peer_offline` 日志。
- `remediation`: `07/01` 请求处理与 `07/81` 应答生成已经离线完成；真实设备发送前仍需合法公共头提供器、受控发送事务和隔离实机兼容性证据。

#### F-011

- `title`: `07/01` 请求可以按旧业务顺序形成可序列化的 `07/81` 待回复动作
- `severity`: n/a_re
- `category`: reverse_algo
- `status`: validated
- `evidence_ids`: E-012, E-013, E-014
- `location`: `ManagerBusiness.messageDeal`, `GVS_Protocol`, `src/gvs_presence.c`, `src/gvs_serialize.c`, `src/runtime_service.c`
- `impact`: Doorfast 已具备离线可验证的探测应答业务语义，同时保持生产网络零发送；这补齐了主动在线维护前的请求解析与回复构造层，但尚未证明真实设备会接受该回复。
- `confidence`: high
- `repro_steps`: 运行 E-012 的静态定位命令核对旧调用顺序，再运行 E-013 的常规和 Sanitizer 测试。
- `remediation`: `r5` 已完成隔离 x86_64 虚拟机接收验收（E-014），`r6` 已完成离线回复队列（E-015），`r7` 已完成模拟发送事务（E-016），`r8` 已接入内存构帧适配层（E-018）；后续定义合法公共头提供器并单独验证兼容性。

#### F-012

- `title`: 探测回复已具有有界、可失效、可去重的离线事务入口
- `severity`: n/a_re
- `category`: design
- `status`: validated
- `evidence_ids`: E-013, E-015
- `location`: `src/gvs_reply_queue.c`, `src/runtime_service.c`
- `impact`: 入站探测不会造成无界内存增长；重复请求和过期回复有确定性行为，未来发送端可从 FIFO 接口接入而无需改写接收解析。
- `confidence`: high
- `repro_steps`: 运行 E-015 的常规和 Sanitizer 测试，核对满队列和失败路径不修改原状态。
- `remediation`: `r7` 已从该队列接入离线发送事务，并在隔离 x86_64 虚拟机验证实际抓包入口到模拟终态（E-017）；`r8` 继续加入内存构帧记录（E-018）。

#### F-013

- `title`: 待回复对象已形成有重试、超时和尝试关联的离线发送闭环
- `severity`: n/a_re
- `category`: design
- `status`: validated
- `evidence_ids`: E-015, E-016, E-017, E-018
- `location`: `src/gvs_reply_queue.c`, `src/gvs_send_transaction.c`, `src/gvs_memory_sender.c`, `src/runtime_service.c`
- `impact`: 未来真实传输适配器可以复用同一事务生命周期；失败、无响应和迟到完成不会被误报为成功，也不会产生无界重试。
- `confidence`: high
- `repro_steps`: 运行 E-016 与 E-018 的常规及 Sanitizer 测试，核对精确 48 字节帧、首次构帧失败后的重试和旧尝试完成被拒绝；再按 E-017 核对 r7 目标链路。
- `remediation`: 保持内存传输边界，定义可替换的合法公共头提供器并验证字段来源。

### Path P-001

- `title`: 选举结束后的同步维护调用路径
- `path_type`: callflow
- `start`: `DF_GVS_PRESENCE_PERIODIC`
- `goal`: 维持同户同步数据，或在维护者失联后接管
- `steps`:
  1. 60 秒截止时由状态机生成 `PERIODIC_SYNC`。evidence: E-001, E-003 — finding: F-002
  2. 数据层按每组最多 20 项构造 `Period` JSON，并封装为 `91/03`。evidence: E-001, E-002, E-003 — finding: F-001
  3. 离线接收器验证帧和 JSON，再按版本/分机号决定应用或重发。evidence: E-001, E-003 — finding: F-001
  4. 非维护者收到有效周期帧后重置 60 秒截止；连续缺失两个周期后接管。evidence: E-001, E-003 — finding: F-002
  5. 运行时记录同步决策，版本变化后原子保存独立 UCI 状态。evidence: E-004 — finding: F-003
  6. 状态快照将在线维护结果映射为脱敏 JSON，供后续本地管理层读取。evidence: E-005 — finding: F-004
  7. 目标构建将快照映射为只读 ubus 响应，LuCI 通过最小 ACL 周期读取并呈现。evidence: E-005, E-006 — finding: F-005
  8. x86_64 目标 APK 在官方 25.12.1 虚拟机由 procd 运行，空闲时仍可响应 ubus 并支持重启和卸载重装。evidence: E-007 — finding: F-006
  9. 隔离回环 UDP 注入验证 `Period` 刷新、`Normal` 版本持久化和两个缺失周期接管。evidence: E-010 — finding: F-009
  10. `07/81` 候选应答从目标抓包入口刷新在线期限，60 秒后自动离线。evidence: E-011 — finding: F-010
  11. `07/01` 请求经完整帧校验后形成 `07/81` 待回复对象；已知候选同时刷新在线状态，生产运行时不发送。evidence: E-012, E-013, E-014 — finding: F-011
  12. 待回复对象进入固定容量队列，重复项刷新期限，过期项由事件循环清理。evidence: E-015 — finding: F-012
  13. 单事务状态机按 FIFO 取出对象，构造并回读校验完整 48 字节 `07/81` 内存帧；模拟结果驱动成功、失败、重试和超时，单调 64 位完成标识隔离迟到结果；r7 目标 APK 已贯通抓包入口到模拟成功终态。evidence: E-015, E-016, E-017, E-018 — finding: F-013
- `residual_risks`: 公共头字段尚未获得合法兼容实现；尚未获得真实设备接受证据；严格 JSON 成员顺序仍需用真实抓包验证。

### Path P-002

- `title`: 目标 APK 升级与回滚恢复路径
- `path_type`: callflow
- `start`: 已运行 `doorfast-0.1.0-r2`
- `goal`: 切换到新二进制，或在失败时恢复旧包和同步状态
- `steps`:
  1. 安装 `r3` 后，APK `post-upgrade` 在真实系统且 `PKG_UPGRADE=1` 时重启 procd 服务。evidence: E-008 — finding: F-007
  2. 新进程重新加载原主配置和同步版本，并恢复只读 ubus。evidence: E-007, E-008 — finding: F-006, F-007
  3. 需要回滚时先备份两份配置，移除当前包并安装旧包，再恢复配置和启动服务。evidence: E-008 — finding: F-007
  4. ubus 返回备份的非零同步版本，随后可再次升级并自动切换进程。evidence: E-008 — finding: F-007
- `residual_risks`: 正式包签名尚未验证；未来配置模式变化仍需专用迁移器；`/tmp` 备份不跨系统重启。

### Path P-003

- `title`: 配置身份到离线对端选举与来电会话的验证路径
- `path_type`: callflow
- `start`: 配置的六字节室内机逻辑身份
- `goal`: 通过生产接口观察同步角色和门口机来电会话状态
- `steps`:
  1. 配置身份启动生产在线维护状态机并生成同户候选。evidence: E-001, E-009 — finding: F-008
  2. 结构化探测、同步询问、版本询问和周期动作进入固定容量模拟器回调。evidence: E-009 — finding: F-008
  3. 已验证格式的合成 `91/81`、`91/82`、`91/03` 帧通过 `df_gvs_runtime_sync_receive()` 返回，`03/01` 来电通过 `df_gvs_receive_datagram()` 返回。evidence: E-009 — finding: F-008
  4. 公开同步状态快照和会话状态生成脱敏 JSON Lines，并以固定逻辑时间验证选举与接管。evidence: E-009 — finding: F-008
  5. `07/01` 请求经过公共头、目标地址和长度校验后生成 `07/81` 待回复动作，已知候选刷新在线期限；运行时保持零发送。evidence: E-012, E-013 — finding: F-011
  6. 待回复动作进入 16 项固定队列，精确重复刷新一秒期限，过期或满载均有确定性结果且不触发网络发送。evidence: E-015 — finding: F-012
  7. 队列对象进入单事务内存适配器，生成完整 `07/81` 并由生产解析器回读；失败、重试及超时路径由离线脚本化结果验证，r7 目标 APK 验证抓包入口可到达模拟终态。evidence: E-015, E-016, E-017, E-018 — finding: F-013
  8. 固定测试帧通过回环转发进入 x86_64 APK，验证同步角色、版本状态和来电事件。evidence: E-010 — finding: F-009
- `residual_risks`: 真实公共头兼容性、主动发送事务和门口机接受性均未验证；隔离 UDP 验证不等同于完整主机模式。

## 6. Timeline 与遗留问题

| 时间 | 事件 |
|---|---|
| 2026-09-08 | 定位周期线程、`syncAll`、单字段更新和协议构帧方法 |
| 2026-09-08 | 实现同步数据存储、版本更新、JSON 分片和 `91/03` 构帧 |
| 2026-09-08 | 接入离线入站解析、维护者仲裁和两个缺失周期接管 |
| 2026-09-08 | 完成常规、边界、Sanitizer、CLI、包清单和协议模型回归 |
| 2026-09-08 | 接入被动抓包循环，并增加独立 UCI 版本状态与掉线恢复 |
| 2026-09-08 | 登记首批敏感同步字段适配器，并增加脱敏运行时状态查询 |
| 2026-09-08 | 增加只读 ubus 状态方法、独立 LuCI 状态页及双 APK 构建清单 |
| 2026-09-08 | 修复空闲抓包阻塞 ubus，并在官方 ImmortalWrt 25.12.1 x86_64 虚拟机完成安装生命周期冒烟 |
| 2026-09-08 | 为核心 APK 增加升级后自动重启，并完成 `r2 → r3` 升级和备份恢复式回滚验收 |
| 2026-09-09 | 完成固定容量离线 GVS 对端模拟器、三种选举/接管场景及同户来电路由回归 |
| 2026-09-09 | 确认 `07/81` 候选在线语义，并在 x86_64 `r4` APK 验证在线刷新与 60 秒离线 |
| 2026-09-09 | 静态确认 `07/01` 回复顺序与载荷来源，完成离线待回复生成及 66 项 C 测试 |
| 2026-09-09 | 完成 16 项固定容量、1 秒失效和精确重复合并的离线回复队列，C 测试增至 68 项 |
| 2026-09-09 | 完成单事务模拟发送、三次尝试、失败与超时终态及跨事务迟到结果隔离，C 测试增至 75 项 |
| 2026-09-09 | 合并 PR #3，并在 ImmortalWrt 25.12.1 x86/64 验证 r7 APK 的抓包、队列和模拟发送事务短流程 |
| 2026-09-09 | 完成完整 48 字节 `07/81` 内存构帧、回读校验和失败重试，C 测试增至 79 项 |

当前实现只接受 `TYPE`、`COUNT`、`INFO` 及其内部字段按旧发送方法的生成顺序出现；真实设备若改变 JSON 成员顺序，需要将解析器扩展为顺序无关。同步版本已经持久化，首批字段已经登记但缺少合法值来源，因此保持默认禁用。本地模拟已经覆盖选举、维护者失联接管、候选在线维护、`07/01` 待回复生成、离线发送事务和来电目标选择，但没有创建网络发送路径。只读状态入口已在 ImmortalWrt 25.12.1 x86_64 虚拟机完成安装、启停、冷启动、卸载重装、升级、备份恢复式回滚及 `07/81` 在线超时验收；正式签名、未来配置迁移、长期运行与真实门口机流量仍未完成。真实公共头兼容性和门口机接受性仍是进入主动网络阶段的主要关口；本阶段不证明完整主机模式。

2026-09-09 目标验收追加：r5 完成 r4 → r5 升级、07/01 待回复接收、07/81 独立接收、同步版本持久化及来电事件验收（E-014）。`r6` 的远程主机与 APK 构建检查已经通过；`r7` 已完成离线发送事务测试（E-016），并在目标虚拟机验证两个探测请求从抓包入口进入独立模拟成功终态（E-017）。`r8` 已完成内存构帧实现与本地验证，等待目标 APK 构建和虚拟机验收。真实设备认可仍未验证。
