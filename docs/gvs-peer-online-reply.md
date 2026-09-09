# GVS 候选设备探测与在线应答

本阶段已接入被动运行时的 `07/01` 请求和 `07/81` 应答接收路径。完整公共头和报文长度校验通过后，要求目的地址等于本机逻辑地址。`07/01` 载荷必须恰为 2 字节；运行时会生成一个仅存在于内存中的待回复对象，但不会向网络发送。`07/81` 载荷必须恰为 6 字节。已知同户候选每次被观察到都会发布在线事件并重置 60 秒倒计时；状态查询的 `online_peers` 随之更新。

## 静态证据

样本为 `com.moorgen.zigbee.gateway`，版本 `4.0.0(26052503)`，SHA-256：`6793c5777bea2c4f56f30c79c19d37d9089976eaaaa61c6da0ef6724a9a8487f`。

相对已解码样本 `smali_classes3/com/gvs/`：

- `vdp/talkback_is/GvsSdk_IS.smali:1155`：功能码 7 注册到 ManagerBusiness。
- `vdp/talkback_is/manager/ManagerBusiness.smali:682-732`：操作码 `0x01` 为 COM_PING_ASK。旧实现先把请求源地址、目标 IP/端口和请求数据交给回复方法，再以请求源地址尝试刷新候选在线状态；因此未知来源也先进入回复路径。
- `vdp/talkback_is/manager/ManagerBusiness.smali:293`：操作码 `0x81` 为 COM_PING_REPLY，以源地址调用候选设备在线更新；此分支不读取载荷。
- `general/protocol/c.smali:2214`：应答序列化为 07/81、6 字节载荷。空 MAC 参数分支复制请求前两字节并补四个零；另一个分支填写六字节 MAC。因此接收器不强制载荷等于固定值。
- `vdp/talkback_is/indoor/b.smali:459`：已知候选地址每次上报在线并重置为 60 秒，无状态变化去重。
- `vdp/talkback_is/indoor/b$1.smali` 的 run：每秒递减，剩余 30 秒倍数时探测，归零时上报离线并重置 60 秒；探测载荷为 00 01。

Doorfast 按相同的业务顺序表达结果：一个有效 `07/01` 总会形成目标为请求源地址的待回复对象；只有请求源地址属于配置身份生成的候选列表时，才同时标记 `peer_observed=true` 并刷新在线期限。回复序列化器构造 48 字节 `07/81`，6 字节载荷为请求前两字节加四个零。生产运行时只记录 `reply_pending=1`，没有网络发送调用。Doorfast 的精确目的地址与载荷长度限制是本项目的输入安全策略，不应据此声称旧业务分支做了相同检查。

## 验证与边界

C 测试覆盖独立手工 `07/01`/`07/81` 报文、回复载荷构造、已知与未知请求源、重复刷新、倒计时到期、非候选/本机源地址、错误目的地址、截断和多余载荷、错误功能码/操作码、事件交付失败不修改状态，以及状态查询在线数量。`r6` 又覆盖固定容量、重复合并、过期、FIFO 取出、时间回退和失败原子性，详见[离线回复队列](gvs-reply-queue.md)；`r7` 覆盖模拟发送成功、失败、重试、超时和尝试关联，详见[离线发送事务](gvs-offline-send-transaction.md)。项目当前共有 75 项 C 测试，常规构建和 AddressSanitizer/UndefinedBehaviorSanitizer 均通过。

公共头内的认证字段仍按不透明字段处理；在线表示观察到结构符合条件的报文，并非认证成功或已获门口机承认。GitHub Actions 运行 `34299375775` 生成的 `doorfast-0.1.0-r5.apk` 已在隔离 ImmortalWrt 25.12.1 x86_64 虚拟机验证：固定 `07/01` 经实际抓包入口产生 `accepted=1 reply_pending=1 peer_observed=1 mode=passive`，固定 `07/81` 独立产生 `peer_reply accepted=1`，状态保持 `online_peers=1`。此次实现没有新增网络发送能力，也不证明真实设备接受性。
