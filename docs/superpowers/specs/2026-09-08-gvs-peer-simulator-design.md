# GVS 离线对端模拟器设计

日期：2026-09-08  
状态：聊天设计已由用户确认，等待书面规格审阅

## 目标与证据边界

本阶段为 Doorfast 新增一个确定性的离线 GVS 对端模拟器，用同一套生产协议解析、序列化、身份、在线维护和会话代码验证以下链路：

1. Doorfast 从配置逻辑身份生成同户候选并开始在线维护。
2. 无其他室内终端时，Doorfast 完成同步选举并成为维护者。
3. 存在同户终端时，`91/81`、`91/82` 和 `91/03` 驱动跟随、优先级比较和失联接管。
4. 模拟门口机按本户逻辑地址发出来电时，完整 42 字节公共头经过解析并进入 `RINGING`；其他住户、错误长度和错误目标不得进入会话。

模拟器只复现已有静态证据和现有 Doorfast 接口支持的语义。两个 8 字节公共头字段使用固定测试值，并在轨迹中标记为 `synthetic`。模拟成功只能证明 Doorfast 内部链路自洽，不能证明真实门口机认可身份、公共头认证兼容或完整主机模式完成。

本阶段不创建 UDP socket、不使用 pcap、不连接真实门禁网络、不修改生产守护进程行为，也不加入猜测出的登录、认证或控制字段。

## 选择的架构

采用“测试支持库 + 场景命令行工具”两层结构。测试支持库持有模拟时间、对端身份和待交付帧队列；命令行工具只选择内置场景并输出 JSON Lines 轨迹。生产模块仍是协议事实来源，模拟器不得复制一套独立解析器。

```mermaid
flowchart LR
    S[场景与逻辑时钟] --> P[Doorfast presence/runtime sync]
    P -->|结构化动作| E[离线对端模拟器]
    E -->|91/81 91/82 91/03| P
    E -->|03/01 来电| R[Doorfast receive/session]
    P --> T[JSON Lines 轨迹]
    R --> T
```

选择纯内存传输是为了让三秒启动等待、三轮同步询问、三轮版本询问、六十秒周期和两轮失联接管可以立即推进，不依赖真实时间和网卡权限。后续隔离 UDP 测试可以消费相同的序列化帧，但不属于本阶段。

## 组件与接口

### 测试支持库

新增 `tests/support/gvs_peer_sim.h` 和 `tests/support/gvs_peer_sim.c`。该库只在测试和工具目标中编译，不进入 `doorfast` 守护进程或 APK。

核心状态包含：

- Doorfast 六字节逻辑身份；
- 一个可选的同户 `0x61` 对端身份及其同步版本；
- 一个模拟门口机 `0x32` 来源身份；
- 当前单调毫秒时间；
- 固定容量帧队列和各动作类型的计数器；
- 对端是否在线、是否回复同步询问、是否发送周期数据等场景开关。

公开接口固定为：

```c
enum df_gvs_peer_sim_scenario {
    DF_GVS_SIM_NO_PEER = 0,
    DF_GVS_SIM_LOWER_PEER,
    DF_GVS_SIM_MAINTAINER_LOSS,
};

struct df_gvs_peer_sim;

int df_gvs_peer_sim_create(struct df_gvs_peer_sim **sim,
                           enum df_gvs_peer_sim_scenario scenario,
                           const uint8_t local[6], uint64_t now_ms);
void df_gvs_peer_sim_destroy(struct df_gvs_peer_sim *sim);
int df_gvs_peer_sim_emit(const struct df_gvs_presence_action *action,
                         void *context);
int df_gvs_peer_sim_advance(struct df_gvs_peer_sim *sim,
                            uint64_t now_ms);
int df_gvs_peer_sim_next_frame(struct df_gvs_peer_sim *sim,
                               const uint8_t **frame, size_t *length);
int df_gvs_peer_sim_make_call(struct df_gvs_peer_sim *sim,
                              const uint8_t destination[6]);
size_t df_gvs_peer_sim_action_count(
    const struct df_gvs_peer_sim *sim,
    enum df_gvs_presence_action_type type);
```

`df_gvs_peer_sim_emit()` 可直接作为 `df_gvs_runtime_sync_tick()` 的动作回调。它只记录动作并按场景把回复序列化进队列，不递归调用 Doorfast。`df_gvs_peer_sim_advance()` 只校验并更新模拟器的单调时间；调用方再推进 Doorfast 计时，通过 `df_gvs_peer_sim_next_frame()` 按 FIFO 顺序取出回复并交给公开接收入口。创建时只分配一个固定容量状态对象，运行期间不扩展队列；销毁函数负责释放它。

现有证据尚未确认 `0x07` 候选探测的入站回复操作码，生产运行时也没有对应解析入口。模拟器因此只记录 `PEER_PROBE` 动作，不构造猜测的在线回复，也不直接调用 `df_gvs_presence_observe_peer()` 伪造网络结果。参与 `91/*` 同步选举的模拟对端不会自动增加 `online_peers`；命令行轨迹将其标记为 `peer_presence_evidence_gap`。

固定容量达到上限、时间倒退、未知动作、序列化失败或无效身份均返回错误，并保持调用前状态。模拟器不动态增长队列。

### 场景命令行工具

新增 `tools/gvs-peer-sim.c`，由 `make peer-sim` 生成 `build/gvs-peer-sim`。接口为：

```sh
build/gvs-peer-sim --scenario no-peer
build/gvs-peer-sim --scenario lower-peer
build/gvs-peer-sim --scenario maintainer-loss
```

工具使用公开测试身份 `IS:2-1-101-2`，不接受接口名、IP 地址、密钥或任意报文输入。命令行工具在每个固定场景里按预定里程碑查询 Doorfast 公开状态并输出 JSON Lines；模拟器库不维护第二份 Doorfast 状态或独立轨迹队列。每行至少包含 `time_ms`、`event`、`phase`、`role`、`version` 和 `online_peers`。同步对端参与选举但缺少已确认 `0x07` 回复时增加 `peer_presence_evidence_gap:true`。来电结果增加 `destination_scope`、`accepted_call` 和 `session_state`。输出不包含两个公共头测试字段或完整原始帧。

命令参数错误返回退出码 2，模拟或协议错误返回退出码 1，场景完成返回 0。输出顺序必须稳定，以便 CI 直接比较。

## 场景与预期结果

### 无其他室内终端

Doorfast 从 `WAIT_SYNC` 依次经过三轮同步询问和三轮版本询问，没有收到对端回复，进入 `PERIODIC` 且角色为 `maintainer`。轨迹必须记录产生的动作数量和最终状态，但不能将其命名为“真实设备上线成功”。

### 低分机号对端

本机使用分机 2，对端使用同户分机 1。对端在同步询问阶段返回合法 `91/81`，或在版本选择阶段返回版本不低于本机的 `91/82`。Doorfast 必须进入或保持 `follower`，对端地址不同户、等于本机或目的地址不精确匹配时不得改变角色。

### 维护者失联

Doorfast 先接受同户对端的周期同步并成为跟随者。模拟器随后停止周期帧，逻辑时钟跨过两个六十秒截止点；Doorfast 必须接管为 `maintainer` 并产生周期同步动作。一次缺失不得提前接管。

### 门口机来电目标选择

模拟门口机生成 `03/01` 零载荷来电帧。测试覆盖三种目的地址：本机完整地址、同户其他分机地址和其他住户地址。现有静态证据表明待机来电按目的地址前五字节匹配，因此前两者应进入 `RINGING`，其他住户应保持 `IDLE`。该规则必须以测试名称明确标注为“同户范围匹配”，不能误写成门口机已选择某一实体分机。

同一场景还要验证 41 字节截断头、声明长度不一致、错误魔数和非 `03/01` 报文不会被接受为来电。

## 数据流与状态所有权

模拟器拥有场景和待交付帧，不拥有 Doorfast 状态。在线阶段、角色、版本、候选数量和会话状态分别由 `df_gvs_runtime_sync`、`df_gvs_presence` 和 `df_gvs_session` 维护。所有回复必须通过 `df_gvs_control_serialize()` 或现有同步序列化接口构造，再通过公开接收入口返回；禁止直接修改 Doorfast 内部字段来伪造成功。

来电帧通过 `df_gvs_receive_datagram()` 进入会话状态机。同步帧通过 `df_gvs_runtime_sync_receive()` 进入在线维护状态机。命令行轨迹从公开状态快照和接收结果生成，不读取私有结构偏移。

## 测试策略

新增 `tests/test_gvs_peer_sim.c` 并接入现有 C 测试二进制，至少覆盖：

1. 无对端时的完整选举动作序列和维护者结果。
2. `91/81` 的跟随者转换。
3. `91/82` 的版本、分机号和阶段门控。
4. 一轮缺失不接管、两轮缺失接管。
5. 探测动作被计数，但同步参与者不会在缺少 `0x07` 证据时增加 `online_peers`。
6. 本机完整地址与同户其他分机来电被接受，其他住户被拒绝。
7. 截断、长度不一致、错误魔数和错误操作码。
8. 时间倒退、队列满和响应生成失败保持状态不变。
9. 固定测试字段不会出现在 JSON Lines 输出。

新增 `tests/test_gvs_peer_sim_cli.sh` 验证三个命令行场景的退出码、关键状态序列、稳定输出及非法参数。现有 `make -B test doorfast`、CLI、包清单、LuCI 和 Python 回归必须继续通过。模拟器不进入 `package/doorfast/Makefile`，APK 清单测试要明确拒绝安装 `gvs-peer-sim`。

AddressSanitizer 和 UndefinedBehaviorSanitizer 使用同一组模拟场景运行，重点检查固定队列边界和帧长度处理。

## 完成标准

本阶段完成必须同时满足：

1. 三个场景通过纯内存确定性运行，重复执行产生相同事件顺序。
2. 在线维护回复和来电帧均经过生产序列化器与生产接收入口，没有直接写内部状态的捷径。
3. 同户来电选择、错误住户拒绝、两个周期后接管及所有错误边界有自动测试。
4. 模拟器和工具不进入 APK，不创建 socket，不访问 pcap，不接受真实凭据。
5. 文档和输出始终使用“离线模拟”“结构验证”等表述，不声称真实门口机认可或完整主机模式完成。
6. 本机完整回归和内存安全检查通过，并形成可复现 Evidence → Finding → Path 记录。

完成本阶段后，下一阶段才把模拟器生成的帧接入隔离虚拟网卡，验证 x86_64 守护进程捕获路径；任何真实 GVS 网络发送仍需单独设计和授权边界。
