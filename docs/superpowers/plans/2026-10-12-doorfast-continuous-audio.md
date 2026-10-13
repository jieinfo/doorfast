# Doorfast 连续音频实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 把正式通话的秒级音频快照改为约 100 ms 的有界连续音频块，并提供严格绑定 runtime/station/generation 的兼容 HTTP 接口。

**架构：** 已通过 GVS 身份接纳的音频先经过有限重排，再进入现有 A-law 缓冲。一个可独立测试的发布器按样本/截止时间输出有限 WAV 块，runtime 负责接线和生命周期；CGI 在读取前后验证同一通话身份。

**技术栈：** C17、现有 G.711 A-law、ubus、POSIX shell、项目 C 测试 runner；不加第三方依赖。

**规格：** [连续音频与免按键双向对讲](../specs/2026-10-12-continuous-duplex-audio-design.md)，用户已确认，规格提交 `f8431b2`。

## 全局约束

- 工作目录：`/Users/shenwenjie/Documents/PVE/doorfast/.worktrees/preview-startup-state`；分支：`codex/preview-startup-state`。复用，不 reset、checkout、stash、clean 或重建。
- 基线：Doorfast `1ab00eb`；按本文执行前只读核实工作树，保留全部既有修改。
- 不改变预览重试、冷却、门禁、保活、上行 20 ms 帧、8302 端口或媒体模块 ABI。
- 最多四个待重排真实包；最长等待 40 ms；每次缺口最多 320 个补偿样本。
- 目标 800 样本/100 ms；未满块等待最多 100 ms；每块最多 1,600 样本/200 ms/3,200 PCM 字节。
- 最多保留 20 块，使用已有私有临时目录，不写持久存储。
- 每段先写失败测试；缺少新 API 引起的初次构建失败属于红色证据，绿阶段必须实际执行测试二进制。
- 仅内置 shell/CLI 和文件工具开发；文件编辑用 apply_patch，禁止操作 Terminal.app。
- 本计划不授权实机呼叫、解锁、配置更改、安装、推送、PR、合并或发布。

## 文件职责与依赖

| 文件 | 职责 | 任务 |
| --- | --- | --- |
| 新 `src/gvs_audio_jitter.{h,c}` | 四槽序号重排、40 ms 截止、短静音事件和自身诊断 | 1 |
| `src/gvs_audio_buffer.{h,c}` | 真实包计数与总样本时钟分离；最新待发布样本读取 | 2 |
| 新 `src/gvs_audio_playout.{h,c}` | 样本/时间触发、有限导出、成功后提交快照 | 2 |
| `src/gvs_audio_chunk_store.{h,c}` | 20 块原子保留，携带块时间与不连续元数据 | 3 |
| `src/runtime_service.{h,c}`、`src/runtime_ubus.{h,c}` | 包接纳后接线、主循环 tick、重置与能力/诊断输出 | 4 |
| `package/doorfast/files/doorfast-http.sh` | v1 音频身份查询、兼容旧查询、读前后校验 | 5 |
| `Makefile`、`tests/test_main.c` | 新源文件/头依赖和测试注册 | 1、2 |
| `tests/test_gvs_audio_jitter.c`、`tests/test_gvs_audio_playout.c` | 新模块的确定性时钟/回调测试 | 1、2 |
| 现有音频 buffer/store/runtime/HTTP 测试 | 兼容性、接线、竞态和失败路径 | 2–5 |
| `README.md`、`docs/gvs-audio-pcap-validation.md` | 当前功能和证据边界，不宣称现场通过 | 6 |

执行顺序为 1 → 2 → 3 → 4 → 5 → 6；完成后才能执行 [HA 实现计划](2026-10-12-ha-continuous-duplex-audio.md) 的真实 v1 接口联调。

## 跨项目接口契约

新的查询同时提供 `runtime`、`station_id`、正整数 `generation`，`after` 可选；两个身份参数缺一或重复参数必须拒绝。runtime 为 16 位小写 hex，station 使用现有 1–32 字符小写 ASCII 标识符规则，数字检查 uint64 范围和布尔歧义。

```text
GET /api/v1/audio/latest.wav?runtime=0123456789abcdef&station_id=gate_main&generation=7&after=30
```

新查询的 200/304 响应必须有 `X-Doorfast-Runtime-Id`、`X-Doorfast-Station-Id`、`X-Doorfast-Generation`。200 还包括以下字段：

```text
X-Doorfast-Audio-Previous-Revision: 30
X-Doorfast-Audio-Revision: 40
X-Doorfast-Audio-Dropped-Bytes: 0
X-Doorfast-Audio-Discontinuity: 0
X-Doorfast-Audio-Timestamp-Ms: 1000
X-Doorfast-Audio-Latest-Timestamp-Ms: 1100
X-Doorfast-Audio-Format: s16le
X-Doorfast-Audio-Sample-Rate: 8000
X-Doorfast-Audio-Channels: 1
```

时间来自同一个 Doorfast 单调时钟，HA 用 latest minus chunk timestamp 判断源端积压，不比较不同主机的墙上时间。200 的 PCM 正文最多 3,200 字节，标准 WAV 总长度最多 3,244 字节。200 的旧 ETag 形式不变；新查询的 304 不能在身份检查前返回。

新查询错误保留 `error` 人类可读文本并增加 `code`：400 `invalid_request`；409 `audio_identity_mismatch` 或 `audio_cursor_expired`；404 `audio_unavailable`；503 `audio_temporarily_unavailable`。当前通话已结束必须优先返回身份失配。可证明仍是同一通话的 404 应带身份头；旧查询维持已有错误和 ETag 契约。

### 任务 1：有限序号重排与短静音事件

**文件：** 新 `src/gvs_audio_jitter.h`、`src/gvs_audio_jitter.c`、`tests/test_gvs_audio_jitter.c`；修改 `Makefile`、`tests/test_main.c`。

- [ ] **步骤 1：先定义失败测试。** 在新测试中注册 `test_gvs_audio_jitter_reorders_and_bounds_gaps`，用回调只记录序号、真实/静音样本数，不写文件。以下声明是本任务要实现的公共 API，不是现有接口：

```c
struct df_gvs_audio_jitter;
struct df_gvs_audio_emission {
    const uint8_t *samples;
    size_t count;
    uint16_t sequence;
    uint64_t generation;
    uint64_t timestamp_ms;
    bool synthetic;
    bool discontinuity;
};
typedef int (*df_gvs_audio_jitter_emit_fn)(
    const struct df_gvs_audio_emission *, void *);
int df_gvs_audio_jitter_init(struct df_gvs_audio_jitter *,
    df_gvs_audio_jitter_emit_fn, void *);
void df_gvs_audio_jitter_reset(struct df_gvs_audio_jitter *, uint64_t);
int df_gvs_audio_jitter_push(struct df_gvs_audio_jitter *,
    const uint8_t *, size_t, uint16_t, uint64_t, uint64_t);
int df_gvs_audio_jitter_tick(struct df_gvs_audio_jitter *, uint64_t);
```

头文件同时定义可在栈上分配的 jitter struct：四槽，每槽最多 256 样本、序号、长度、到达时间和占用标志；其余字段为绑定 generation、下一个期望序号、缺口截止时间、最近真实包长度、单调时钟和重复/迟到/重排计数、已经成功发送的本次缺口静音数、回调及 context。回调不得保留 samples 指针；emission.timestamp_ms 为实际输出时刻，原到达时间独立保留用于缺口截止。

测试先用 seq 10/12/11，时间 100/120/135 ms，断言真实输出为 10/11/12 且无静音；再用 10/12 后 tick 到 159/160 ms，断言截止前无 12、截止时输出一次有限静音和 12。分别用 128/160/192 样本，覆盖四槽溢出、重复、半范围旧包、65535→0、超大 gap、时钟倒退、callback 失败、错误 generation 和 reset 后无旧输出。

```c
/* tests/test_gvs_audio_jitter.c 中的一个实际测试段。 */
uint8_t samples[160];
memset(samples, 0xd5, sizeof(samples));
TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(
    &jitter, samples, sizeof(samples), 10, 7, 100));
TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_push(
    &jitter, samples, sizeof(samples), 12, 7, 120));
TEST_ASSERT_INT_EQ(1, trace.real_count);
TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 159));
TEST_ASSERT_INT_EQ(1, trace.real_count);
TEST_ASSERT_INT_EQ(0, df_gvs_audio_jitter_tick(&jitter, 160));
TEST_ASSERT_INT_EQ(2, trace.real_count);
TEST_ASSERT_INT_EQ(160, trace.synthetic_samples);
```

`trace` 是本测试文件定义的回调记录 struct：`int real_count; int synthetic_samples; uint16_t sequences[16];`，回调对 synthetic 累加样本，对 real 保存序号并累加计数，超过容量返回 -1。

- [ ] **步骤 2：运行红色测试。** 将测试声明/调用加入 runner、测试源加入 Makefile 后运行 `make test`；预期缺少新模块/API，记录真实错误，不把拼写或环境错误当行为失败。
- [ ] **步骤 3：实现重排。** 先识别 generation/时钟和长度，再按 `(uint16_t)(sequence - expected)` 判断正向半区间；距离 ≥ 32768 视为旧包。缺口最长 40 ms，窗口满时推进最近后继；缺失样本估计 `missing_packets * last_real_samples`，使用饱和计算并截到 320，发送 `0xd5` 静音后发送真实包。超上限设置 discontinuity；不生成无限填充，不推进虚拟真实包数。失败回调不能提前移除真实待发包或推进其序号；已成功输出的静音不能因后续真实包回调失败而重复输出，这一分段失败必须单独测试。

```c
uint16_t distance = (uint16_t)(sequence - expected_sequence);
if (distance >= 32768U) return reject_late_packet(jitter);
/* reject_late_packet 为本模块静态 helper：递增迟到计数并返回非致命拒绝。 */
```

- [ ] **步骤 4：验证绿及容量。** `make test && build/doorfast-tests`；检查所有循环最多访问四槽、所有 copy 不超过 256 样本。新头文件列入测试和 daemon 目标依赖，暂不加未用到的 daemon 源。
- [ ] **步骤 5：提交精确文件。** `git add src/gvs_audio_jitter.h src/gvs_audio_jitter.c tests/test_gvs_audio_jitter.c tests/test_main.c Makefile && git commit -m "feat(audio): bound receive reordering and gap concealment"`。

### 任务 2：分离样本时钟并实现小块发布器

**文件：** 修改 `src/gvs_audio_buffer.{h,c}`、`tests/test_gvs_audio_buffer.c`；新 `src/gvs_audio_playout.{h,c}`、`tests/test_gvs_audio_playout.c`；修改 `Makefile`、`tests/test_main.c`。

- [ ] **步骤 1：编写失败测试。** 保留现有 buffer 测试。新增 `sample_count`、`snapshot_source_samples`、`concealed_samples` 字段，真实 `byte_count` 不包含合成静音。buffer 新增 `df_gvs_audio_buffer_push_silence(b,count,generation)` 与 `df_gvs_audio_buffer_copy_pending_tail(b,out,capacity,length)`；旧 copy 函数继续保持原有顺序语义。

```c
TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push(
    &buffer, real, 160, 10, 7, 100));
TEST_ASSERT_INT_EQ(0, df_gvs_audio_buffer_push_silence(&buffer, 160, 7));
TEST_ASSERT_INT_EQ(160, (int)buffer.byte_count);
TEST_ASSERT_INT_EQ(320, (int)buffer.sample_count);
TEST_ASSERT_INT_EQ(1, (int)buffer.packet_count);
```

`real` 为测试内 `uint8_t real[160]`，填充 `0xd5`。测试 copy_pending_tail 始终返回最新且未发布的样本；mark_snapshot 成功后 pending 归零、原真实 revision 正确、WAV 长度使用所有输出样本计算。

发布器测试用 128 样本进入后 tick 到 99/100 ms，断言短首块只在截止到达时发布；连续五包 160 样本发布 800 样本；积压 3,200 样本最多导出最新 1,600；失败 callback 不提交 revision且至少100 ms后才再尝试；reset 新 generation 不沿用旧时钟或失败重试截止。

- [ ] **步骤 2：运行红色测试。** `make test`；预期新字段/helper 或发布器行为尚不存在。
- [ ] **步骤 3：实现有限发布器。** 公共数据和 API 定义如下；`df_gvs_audio_playout` struct 持有 buffer 指针、回调/context、当前 generation、最老待发布时间、下一次失败重试时间和不连续标志，不拥有第二份无限缓存。

```c
struct df_gvs_audio_chunk {
    uint64_t generation, previous_revision, revision, timestamp_ms;
    size_t sample_count;
    uint64_t dropped_samples;
    bool discontinuity;
};
typedef int (*df_gvs_audio_publish_fn)(const uint8_t *,
    const struct df_gvs_audio_chunk *, void *);
int df_gvs_audio_playout_init(struct df_gvs_audio_playout *,
    struct df_gvs_audio_buffer *, df_gvs_audio_publish_fn, void *);
void df_gvs_audio_playout_reset(struct df_gvs_audio_playout *, uint64_t);
int df_gvs_audio_playout_push(struct df_gvs_audio_playout *,
    const struct df_gvs_audio_emission *);
int df_gvs_audio_playout_tick(struct df_gvs_audio_playout *, uint64_t);
```

push 对 real 走原 buffer_push，对 synthetic 走 push_silence；所有字节/sample 累加先检查 uint64 溢出。pending 的差值改用 sample_count，快照真实 source bytes 仍来自 byte_count。tick 对 pending≥800 或 oldest+100 ms 到期导出尾部最多 1,600 样本，callback 成功后才 mark_snapshot；记录被丢弃样本、重排产生的不连续；禁止没有新真实 revision 的发布。publish callback 必须同步、非重入且不修改 buffer；所有快照提交前提先于文件发布验证，不能文件成功后才发现可预知的提交错误。临时发布失败保留游标，下一次尝试至少间隔100 ms，避免主循环每10 ms重复文件操作；截止计算也检查uint64溢出。

```c
bool due = pending_samples >= 800U ||
    (now_ms >= oldest_pending_ms && now_ms - oldest_pending_ms >= 100U);
```

- [ ] **步骤 4：运行全部 C 测试。** `make test && build/doorfast-tests`；补齐 ring 回绕、capacity<available 的尾部选择、只有静音不造 revision 和 UINT64 边界断言。
- [ ] **步骤 5：提交精确文件。** `git add src/gvs_audio_buffer.h src/gvs_audio_buffer.c src/gvs_audio_playout.h src/gvs_audio_playout.c tests/test_gvs_audio_buffer.c tests/test_gvs_audio_playout.c tests/test_main.c Makefile && git commit -m "feat(audio): publish bounded sample-clock chunks"`。

### 任务 3：原子保存 20 块及不连续元数据

**文件：** `src/gvs_audio_chunk_store.{h,c}`、`tests/test_gvs_audio_chunk_store.c`。

- [ ] **步骤 1：编写失败测试。** 用 21 次发布验证只保留最近 20 块；原 `publish` 仍产生六段旧文件名。新增 v1 发布 API，并验证八段文件名、时间/flag、重复 revision 拒绝、裁剪失败撤回新块、符号链接目录拒绝和 clear 不越界。

```c
int df_gvs_audio_chunk_store_publish_v1(const char *directory,
    const char *source, uint64_t generation, uint64_t previous_revision,
    uint64_t revision, size_t bytes, uint64_t dropped_samples,
    uint64_t timestamp_ms, bool discontinuity);
/* v1: chunk-7-30-40-1644-0-1000-0.wav */
```

- [ ] **步骤 2：运行红色测试。** `make test`；现有四块保留上限或缺少新 API 应导致失败。
- [ ] **步骤 3：实现。** `DF_GVS_AUDIO_CHUNK_LIMIT` 改为 20；文件名解析同时接受六段旧格式和八段 v1 格式。复用 mkstemp/copy/rename/private-dir 检查；v1 bytes 必须为 44 + 2×样本数且 ≤3,244；时间为 uint64、flag 只允许 0/1。旧 publish 不更改调用签名。循环裁剪只针对已验证的 chunk 文件，不扩大目录删除范围。

```text
chunk-<generation>-<previous>-<revision>-<wav_bytes>-<dropped_samples>-<timestamp_ms>-<discontinuity>.wav
```

- [ ] **步骤 4：验证绿。** `make test && build/doorfast-tests`；调整原 pruning-failure 测试的初始块数为上限减一，确保仍真的触发 unlink 失败，而非因上限变化变成空断言。
- [ ] **步骤 5：提交。** `git add src/gvs_audio_chunk_store.h src/gvs_audio_chunk_store.c tests/test_gvs_audio_chunk_store.c && git commit -m "feat(audio): retain bounded v1 chunks with continuity metadata"`。

### 任务 4：接入运行时、生命周期及能力状态

**文件：** `src/runtime_service.c`、`src/runtime_ubus.{h,c}`、`src/gvs_audio_buffer.h`、`tests/test_runtime_service.c`、`tests/test_runtime_ubus.c`、`Makefile`。

- [ ] **步骤 1：编写失败测试。** 在 runtime_service 现有可测试 helper 模式下，增加 `df_runtime_audio_service(jitter,playout,now_ms)`，验证先释放到期重排再发布，不阻塞其他主循环工作；测试音频 status 的真实/合成统计分离和 generation 清理。ubus 新增 `df_runtime_ubus_bind_audio_jitter(service,jitter)`，只能绑定一次，用于将 jitter 拒绝统计合并到 audio 状态。

```c
int df_runtime_audio_service(struct df_gvs_audio_jitter *jitter,
    struct df_gvs_audio_playout *playout, uint64_t now_ms);
TEST_ASSERT_INT_EQ(0, df_runtime_audio_service(&jitter, &playout, 200));
```

以上声明放入 `src/runtime_service.h`，因此本任务也修改该头文件。测试分别覆盖会话结束、抢占、generation 变化、两种入站路径和没有后续包时的截止推进。

- [ ] **步骤 2：运行红色测试。** `make test`；预期 runtime helper 和 jitter 状态绑定尚缺失。
- [ ] **步骤 3：实现接线。** 主服务初始化 jitter 与 playout，真实/合成 emission callback 只调用 playout_push；publish callback 复用 `df_g711_alaw_write_wav` + `publish_v1`。原包解析和 `df_gvs_media_admit` 通过后才 jitter_push。替换两个接收路径的旧秒级导出；每轮主循环调用 service tick；media_clear 对 buffer/jitter/playout 同步重置，挂断丢弃 pending，所有旧 `last_audio_export_ms` 路径删除。

```c
int df_runtime_audio_service(struct df_gvs_audio_jitter *j,
    struct df_gvs_audio_playout *p, uint64_t now_ms) {
    int result = df_gvs_audio_jitter_tick(j, now_ms);
    if (result != 0) return result;
    return df_gvs_audio_playout_tick(p, now_ms);
}
```

audio status 新增 `pcm_stream_version=1`、`concealed_samples`、`reordered_packets`、`snapshot_discontinuity`；保留原计数名，合并 jitter duplicate/late 计数且不重复累计。主循环对临时音频发布IO失败继续运行并有界记录错误，不能因此中断视频、门禁或保活；generation重置仍必须成功。新模块加到 DAEMON_SOURCES 和 TEST_SOURCES；APK 主包使用 src glob 已自动包含新 .c，不把新模块误加入可选视频 .so 的移除列表。

- [ ] **步骤 4：构建及回归。** `make test && build/doorfast-tests && make doorfast && sh tests/test_package_manifest.sh`；确认主程序和 recorder 链接完整、媒体 ABI 测试未变。仅测试静态 helper 不算整个实机循环证明，最终验收单独记录。
- [ ] **步骤 5：提交。** 精确暂存本任务文件含 `src/runtime_service.h`，提交 `feat(audio): wire bounded playout into call lifecycle`。

### 任务 5：v1 CGI 身份校验与兼容读取

**文件：** `package/doorfast/files/doorfast-http.sh`、`tests/test_doorfast_http.sh`。

- [ ] **步骤 1：编写失败测试。** 扩充原 fake ubus/jsonfilter，加入 runtime、call.station_id、call.session、pcm_stream_version 和音频时间字段；新模拟变量使用 TEST_AUDIO_*，不覆盖系统环境变量。用 fake cp 在读取中途触发 status 改变，分别保持同 generation 改 runtime、改 station 和改为 ended。

```sh
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  REQUEST_METHOD=GET PATH_INFO=/api/v1/audio/latest.wav \
  QUERY_STRING='runtime=0123456789abcdef&station_id=gate_main&generation=7&after=30' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-v1"
grep -aFq 'X-Doorfast-Runtime-Id: 0123456789abcdef' "$workspace/audio-v1"
grep -aFq 'X-Doorfast-Station-Id: gate_main' "$workspace/audio-v1"
```

测试身份参数缺一、重复/空/越界/注入值、旧六段和新八段文件名、304 身份头、挂断优先于音频 ready=false、游标过期稳定 code、临时文件读失败与实测长度上限。保留已有全部 legacy 断言。

- [ ] **步骤 2：运行红色测试。** `sh tests/test_doorfast_http.sh`；预期 v1 身份头缺失或错误身份未拒绝。
- [ ] **步骤 3：实现。** 音频路径解析单独增加 identity 参数，不能改动控制/视频 query 语义。将读取 status 的成功和 snapshot_ready 的判断拆开：先核验 talking/runtime/station/generation，再处理 404/304/after；复制后重新核验同一身份。选择 v1 chunk 时严格解析 timestamp/flag；校验最大 WAV 尺寸，计算 nonnegative latest timestamp。新错误仅在 identity 查询增加 code，旧调用者维持原错误。

```sh
# 放在 snapshot_ready 与 ETag 分支之前；变量均来自已验证查询/status。
if [ "$CALL_SESSION" != talking ] ||
   [ "$STATUS_RUNTIME" != "$REQUESTED_RUNTIME" ] ||
   [ "$CALL_STATION" != "$REQUESTED_STATION" ] ||
   [ "$CALL_GENERATION" != "$REQUESTED_GENERATION" ]; then
  audio_v1_error '409 Conflict' audio_identity_mismatch
  exit 0
fi
```

`audio_v1_error(status,code)` 为本任务新增 shell helper：仅使用上述固定 code，输出 JSON 和正确 CGI status；有已验证同身份时输出身份头，无证明时不伪造。`audio_v1_headers` 负责统一 200/304/同身份 404 的头，所有分支调用同一 helper。

- [ ] **步骤 4：验证绿。** `sh tests/test_doorfast_http.sh && sh tests/test_package_manifest.sh && git diff --check`；检查 `sh -n package/doorfast/files/doorfast-http.sh`。失败/重启测试不得依赖真实设备或凭据。
- [ ] **步骤 5：提交。** `git add package/doorfast/files/doorfast-http.sh tests/test_doorfast_http.sh && git commit -m "feat(audio): validate station PCM identity across HTTP reads"`。

### 任务 6：集中回归和可复现证据

**文件：** 新 `tests/test_audio_receive_pipeline.c`；修改 `tests/test_main.c`、`Makefile`、`README.md`、`docs/gvs-audio-pcap-validation.md`；必要的缺陷修复限于任务 1–5 文件。

- [ ] **步骤 1：增加组合测试。** 使用真实 GVS audio serialize/parse、真实 jitter/buffer/playout 和 fake publisher，一次性喂入三种载荷、乱序、缺口、重复、generation 切换，断言 PCM 样本内容与数量、回调时间和无旧 generation 输出。fake publisher 不写磁盘，仅计算摘要/计数，不记录真实语音。

```c
TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 200));
TEST_ASSERT_INT_EQ(1, published.count);
TEST_ASSERT_INT_EQ(7, (int)published.last.generation);
TEST_ASSERT_INT_EQ(1, published.last.sample_count <= 1600U);
df_gvs_audio_jitter_reset(&jitter, 8);
df_gvs_audio_playout_reset(&playout, 8);
TEST_ASSERT_INT_EQ(0, df_gvs_audio_playout_tick(&playout, 400));
TEST_ASSERT_INT_EQ(1, published.count);
```

`published` 为本测试的记录器：`unsigned count; struct df_gvs_audio_chunk last;`，callback 复制元数据并累加计数；单独记录 checksum 验证值，无 PCM 日志。此测试没有刻意失败时，使用 apply_patch 临时破坏 generation reset，确认测试失败，再立即恢复，证明断言确实约束旧音频泄漏。

- [ ] **步骤 2：运行全量验证。** 下列命令在主程序工作目录执行，明确重新运行 C runner：

```sh
set -e
make test
build/doorfast-tests
make doorfast
sh tests/test_doorfast_http.sh
sh tests/test_package_manifest.sh
node tests/test_luci_status.js
node tests/test_luci_media.js
node tests/js/test_media_status.mjs
git diff --check
```

- [ ] **步骤 3：更新证据与审查。** README 描述 v1 能力和未完成的 HA/现场验收；音频 PCAP 附录区分已有厂商证据与新合成测试。独立审查重点是 uint64/序号边界、四槽、20 块、真实计数、callback 失败、所有 reset 调用和 CGI 重启竞态。修复审查问题后重新运行相关测试。
- [ ] **步骤 4：提交与交接。** 精确暂存实际变动文件，提交 `test(audio): cover bounded receive pipeline and compatibility`；记录本地 SHA 和测试退出码，转交 HA 计划，不推送、不安装、不声称物理扬声器通过。

## 自检覆盖索引

接收/重排/补偿 → 任务 1、2；首块/样本时钟/积压 → 任务 2；原子保留/裁剪/目录安全 → 任务 3；生命周期/能力/原链路保护 → 任务 4、6；身份/304/兼容/重启 → 任务 5；PCM 上限与 timestamp/discontinuity → 任务 2、3、5；现场听音 → 完成本计划后独立授权验收，不以单元测试替代。
