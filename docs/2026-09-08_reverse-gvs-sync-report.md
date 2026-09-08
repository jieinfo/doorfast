# GVS 周期同步与版本维护逆向分析报告

> 分析日期：2026-09-08
> 报告类型：普通协议逆向，`flavor = null`
> 验证方式：APK 静态调用链、Doorfast C 单元测试和内存回放

## 1. 执行摘要

本阶段确认并实现了 GVS 室内终端在选举完成后的同步数据链路。同步数据使用 `91/03` 控制帧，载荷由两字节小端版本和 US-ASCII JSON 组成；全量周期数据以 20 项为一组分片，单字段变化使用 `Normal` 类型。Doorfast 已能在内存中登记项目维护的同步字段、更新版本、构造两类 JSON、解析自身兼容格式的入站同步帧，并按旧 APK 的版本和分机号规则选择接受或重发。首批两个静态确认字段已经进入项目适配器目录，但作为敏感字段默认禁用且拒绝空值启用。运行时新增脱敏状态快照与 JSON 查询，为后续 ubus/LuCI 展示同步阶段、维护角色和在线候选数量建立边界。真实公共头认证、UDP 发送及设备接受性仍待独立验证。

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
sh tests/test_package_manifest.sh
python3 -B -m unittest discover -s tests -p 'test_gvs_preemption_model.py'
```

另使用 AddressSanitizer 和 UndefinedBehaviorSanitizer 运行同一套 52 项 C 测试。本地编译、内存回放、命令行测试、APK 软件包清单测试和 Python 协议模型测试均纳入收尾验证。

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
- `category`: validation
- `status`: validated
- `evidence_ids`: E-006, E-007
- `location`: `src/capture.c`, `src/runtime_service.c`, `package/doorfast`, `package/luci-app-doorfast`
- `impact`: Doorfast 核心和只读状态页可以在官方 ImmortalWrt 25.12.1 x86_64 环境安装并由 procd 持续运行；空闲抓包不会再阻塞 ubus，合成 GVS 来电帧可从目标网卡进入会话状态机。该结论仅覆盖被动平台生命周期和合成接收，不证明真实门口机互操作、主动上线或完整主机模式。
- `confidence`: high
- `repro_steps`: 使用 E-007 的官方镜像、APK 和命令复现首次安装、启停、冷启动及卸载重装。
- `remediation`: 正式发布前补签名信任、版本升级/降级、长期运行和真实网络流量验收。

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
- `residual_risks`: 公共头字段尚未获得合法兼容实现；尚未接入 UDP 和真实设备；严格 JSON 成员顺序仍需用真实抓包验证。

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

当前实现只接受 `TYPE`、`COUNT`、`INFO` 及其内部字段按旧发送方法的生成顺序出现；真实设备若改变 JSON 成员顺序，需要将解析器扩展为顺序无关。同步版本已经持久化，首批字段已经登记但缺少合法值来源，因此保持默认禁用。只读状态入口已在 ImmortalWrt 25.12.1 x86_64 虚拟机完成安装、启停、冷启动和卸载重装验收；正式签名、升级/降级、长期运行与真实流量仍未完成。公共头认证字段和真实设备接受性仍是进入网络发送前的主要关口；本阶段不证明完整主机模式。
