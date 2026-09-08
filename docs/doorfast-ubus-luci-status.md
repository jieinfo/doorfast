# Doorfast 运行状态页使用说明

> 适用系统：ImmortalWrt 25.12.1 x86_64
>
> 能力边界：此页面只读取 Doorfast 的被动观察与同步维护状态。它不能修改配置、发送开门命令或向 GVS 网络发包。本阶段不证明门口机已接受 Doorfast，也不代表完整主机模式已经完成。

## 安装状态页

把同一次构建产生的两个 APK 上传到路由器，然后在其所在目录执行：

```sh
apk add ./doorfast-0.1.0-r3.apk ./luci-app-doorfast-0.1.0-r1.apk
```

正式发布包应使用项目发布密钥签名。GitHub Actions 生成的开发测试包使用临时构建密钥，未把该密钥加入测试机信任库时，只能在隔离测试环境用 `apk add --allow-untrusted` 安装；不要把这一选项用于正式发布流程。

核心包默认不启动。先查看 `/etc/config/doorfast`，按本机情况填写接口和六字节逻辑地址，再启用服务：

```sh
uci set doorfast.main.enabled='1'
uci set doorfast.main.gvs_interface='br-lan'
uci set doorfast.main.gvs_local_address='IS:2-1-101-1'
uci commit doorfast
/etc/init.d/doorfast restart
```

`br-lan` 和示例地址仅用于说明格式，必须换成用户自己系统中的接口及本户合法地址。逻辑地址格式为 `IS:楼栋-单元-房间-分机`；Doorfast 不限定 `eth0` 或 `wlan0`。

## 查看运行状态

命令行可直接读取唯一的只读方法：

```sh
ubus call doorfast status '{}'
```

LuCI 页面位于“服务 → Doorfast”，对应路径：

```text
/cgi-bin/luci/admin/services/doorfast
```

页面每 5 秒刷新一次。刷新失败但已有上一次结果时，页面保留该结果并标记“陈旧”；首次读取失败或返回结构无效时，页面提示服务未运行或状态接口不可用。停止服务后，`doorfast` ubus 对象应消失：

```sh
/etc/init.d/doorfast stop
ubus call doorfast status '{}'
```

## 理解状态字段

| 字段 | 含义 | 当前约束 |
|---|---|---|
| `running` | Doorfast 进程能否提供状态 | 当前正常响应时为 `true` |
| `mode` | 网络运行模式 | 当前固定为 `passive`，即只观察、不发送 |
| `sync.phase` | 在线维护阶段 | `down`、`wait_sync`、`sync_ask`、`sync_choose` 或 `periodic` |
| `sync.role` | 当前同步角色 | `down`、`starting`、`maintainer` 或 `follower` |
| `sync.version` | 本地同步版本 | 无符号整数，来自脱敏运行状态 |
| `sync.periodic_misses` | 连续缺失的周期同步次数 | 仅表示离线状态机判断，不等同于网络故障结论 |
| `sync.online_peers` | 当前观察到的同户候选数量 | 不证明门口机已经承认本机身份 |
| `sync.registered_adapters` | 项目内登记的同步适配器数量 | 不包含适配器名称和值 |
| `sync.enabled_adapters` | 当前启用的适配器数量 | 敏感适配器默认禁用 |
| `sync.last_opcode` | 最近一次同步功能操作码 | 无同步报文时可为初始值 |
| `sync.last_handled` | 最近报文是否被处理 | 布尔值 |
| `sync.last_accepted` | 最近报文是否被接受 | 布尔值 |
| `sync.last_rejected` | 最近报文是否被拒绝 | 布尔值 |
| `sync.resend_local` | 状态机是否判定应重发本地数据 | 被动模式只记录判定，不实际发送 |

状态接口和 LuCI ACL 都是只读的。响应不会返回同步字段名称、同步值、密钥或完整私有报文。

## 排查不可用状态

先检查服务是否启用、进程日志以及 ubus 对象：

```sh
uci get doorfast.main.enabled
/etc/init.d/doorfast status
logread -e doorfast
ubus list doorfast
```

Doorfast 即使暂时连不上 ubus，也会继续执行被动观察，并每 5 秒尝试重新连接。接口不可用时应以日志和状态阶段共同诊断；不要把 `role=down` 单独解释为设备拒绝。

## 升级与回滚

从 `r3` 开始，安装较新核心包后会自动重启已启用的 Doorfast 服务，使新二进制立即生效。升级前仍应备份两份配置：

```sh
cp /etc/config/doorfast /tmp/doorfast.backup
cp /etc/config/doorfast-sync /tmp/doorfast-sync.backup
apk add ./doorfast-0.1.0-r3.apk ./luci-app-doorfast-0.1.0-r1.apk
ubus call doorfast status '{}'
```

ImmortalWrt 25.12.1 的 apk-tools 3 不支持原地降级参数。回滚到旧包需要先移除当前包，再安装旧包；安装完成后停止服务、恢复备份并重新启动：

```sh
/etc/init.d/doorfast stop
apk del luci-app-doorfast doorfast
apk add ./doorfast-0.1.0-r2.apk ./luci-app-doorfast-0.1.0-r1.apk
/etc/init.d/doorfast stop
install -m 600 /tmp/doorfast.backup /etc/config/doorfast
install -m 600 /tmp/doorfast-sync.backup /etc/config/doorfast-sync
/etc/init.d/doorfast start
ubus call doorfast status '{}'
```

上述备份放在 `/tmp`，只适合不重启设备的短时回滚；正式升级应复制到持久存储。虚拟机已验证 `r2 → r3` 自动重启，以及保留同步版本 `321` 的 `r3 → r2 → r3` 恢复流程。

## 卸载

先停止服务，再移除界面和核心包：

```sh
/etc/init.d/doorfast stop
apk del luci-app-doorfast doorfast
```

卸载会保留用户修改过的配置文件；保持软件包默认内容、从未修改的配置则可能被移除。本项目已分别观察到修改后的 `/etc/config/doorfast` 和同步版本非零的 `/etc/config/doorfast-sync` 被保留，而默认同步状态会被移除。因此无论当前值是否为默认值，卸载或回滚前都应备份两者。版本升级与回滚已经验收，未来配置模式发生变化时仍需补迁移测试。
