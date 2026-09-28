# Doorfast 稳定预览发布实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 让一次 Doorfast 预览发布跨过门口机短流和 `03/50` 忙窗口，已播放的画面切为“正在重连”并在真实帧返回时自动恢复。

**架构：** 公开 `session.generation` 和编码器绑定的发布代次保持不变，`monitor.generation`／`media_generation` 只标识门口机控制尝试。首次真实 JPEG 才启动编码器；此后仅重置尝试侧分片，按输入帧率把同尺寸提示 JPEG 送给原编码器。真实帧、提示帧和发布是否可播放分别计量。

**技术栈：** C17、媒体 ABI v3、FFmpeg RTSP、libjpeg-turbo、OpenWrt SDK、现有 `make test` 测试框架。

**规格：** `docs/superpowers/specs/2026-09-28-preview-continuity-design.md`；HA 配套计划是 `docs/superpowers/plans/2026-09-28-ha-preview-continuity.md`。

## 全局约束

- 保持媒体 ABI v3 结构布局；不向 `df_media_session_status_v3` 追加字段。
- 公开 generation、station id、stop 和来电优先级与现有 API 兼容。
- 首次真实 JPEG 前不启动发布；提示帧不增加 `frames_received`、不刷新 `last_frame_ms`、不算首帧成功。
- 每个预览最多缓存一张提示 JPEG；沿用现有编码器写管道背压，不积累无界队列。
- 提示 JPEG 的输入宽高取当前发布的实际输入宽高，不能写死样本的 `640×480`。
- 最后观看者宽限期结束、显式 stop、来电抢占、卸载或真正失败必须关闭提示源、FFmpeg 和门口机尝试；来电永不收到提示帧。
- 主动 `03/51` 只可作为单独可回滚 A/B 试验，未达规格门槛不可并入此生产修复。
- 不在计划执行阶段擅自改实机；CI 通过后以 PR 产物做可回滚实测，HA HACS 更新由用户执行。

## 文件职责

| 文件 | 职责 |
| --- | --- |
| `src/media_reconnect_frame.h/.c` | 按任意受支持输入尺寸生成一张带“正在重连”字样的 JPEG；由调用者持有并释放；尺寸、内存和 JPEG 错误有界。 |
| `src/media_session.h/.c` | 保存发布与尝试的分离状态、提示帧缓存、实时／重连切换和帧率节拍；不改变 ABI v3 公开结构。 |
| `src/media_session_manager.c` | 在 `03/02`、忙、stall、重试确认中只重置尝试侧；保持公开发布；终止事件仍彻底清理。 |
| `src/media_module.h`、`src/runtime_ubus.c`、`src/runtime_service.c` | 在不改变 ABI v3 布局的前提下追加明确的提示帧生成失败错误码及日志/API 映射。 |
| `src/gvs_monitor.c/.h` | 仅在现有重试动作不足以表达尝试边界时调整；维持受控退避和 `03/50` 处理，不加入主动心跳。 |
| `tests/test_media_reconnect_frame.c` | 验证真实可解码的提示 JPEG、文字可见性和尺寸／错误边界。 |
| `tests/test_media_session.c`、`tests/test_media_session_manager.c`、`tests/test_main.c` | 验证发布代次、首帧、短流恢复、资源终止和三站容量。 |
| `Makefile`、`package/doorfast-media/Makefile`、`.github/workflows/build-apk.yml` | 链接 JPEG 库并让本地及 OpenWrt PR 构建包含同一实现。 |
| `docs/superpowers/specs/2026-09-28-preview-continuity-design.md` | 验收准则；实现发现矛盾时先修改规格并让用户确认。 |

### 任务 1：动态提示 JPEG 生成器

**文件：** 创建 `src/media_reconnect_frame.h`、`src/media_reconnect_frame.c`、`tests/test_media_reconnect_frame.c`；修改 `Makefile`、`package/doorfast-media/Makefile`、`.github/workflows/build-apk.yml`、`tests/test_main.c`。

- [ ] **步骤 1：编写失败的测试。** 注册 `test_media_reconnect_frame_dimensions_and_bounds()`；对 `640×480` 和 `800×600` 调 `df_media_reconnect_frame_create()`，用 libjpeg 解码并断言宽高、中心文字笔画与背景像素不同，输出大小 `<= DF_GVS_VIDEO_MAX_FRAME`；对 `0×480`、`UINT16_MAX×UINT16_MAX` 断言失败且输出被清零。测试只读取像素，不依赖固定 JPEG 字节。

```c
struct df_media_reconnect_frame frame = {0};
TEST_ASSERT_INT_EQ(DF_OK, df_media_reconnect_frame_create(800U, 600U, &frame));
TEST_ASSERT_INT_EQ(800, frame.width);
TEST_ASSERT_INT_EQ(600, frame.height);
TEST_ASSERT_INT_EQ(0, df_gvs_jpeg_validate(frame.data, frame.length));
df_media_reconnect_frame_destroy(&frame);
```

- [ ] **步骤 2：运行 `make test`，预期因新头文件或函数未定义失败。**
- [ ] **步骤 3：实现有界 JPEG 生成器。** 头文件导出下面的所有权接口。C 文件使用 libjpeg 逐行压缩 RGB（只分配一行 `width * 3`，不分配整幅 RGB）、内置四个 16×16 汉字笔画位图绘制“正在重连”，深色背景与浅色文字；缩放和居中仅用整数算术。压缩前检查 `width >= 64`、`height >= 64`、`width * height <= 4096U * 2160U` 及扫描行分配乘法溢出；libjpeg 错误用 `setjmp` 错误出口释放全部内存；压缩后检查 `length <= DF_GVS_VIDEO_MAX_FRAME`。不要调用 shell、运行第二个 FFmpeg 或读取外部字体。

```c
struct df_media_reconnect_frame {
    uint8_t *data;
    size_t length;
    uint16_t width;
    uint16_t height;
};
int df_media_reconnect_frame_create(uint16_t width, uint16_t height,
    struct df_media_reconnect_frame *out);
void df_media_reconnect_frame_destroy(struct df_media_reconnect_frame *frame);
```

- [ ] **步骤 4：接入构建并运行 `make test && ./build/doorfast-tests`。** `Makefile` 的 `TEST_SOURCES` 加入生成器及测试，测试链接 `-ljpeg`（macOS `/opt/homebrew` 头／库路径仅在存在时加入）；媒体包的 `DF_MEDIA_SOURCES` 加入生成器、链接 `-ljpeg`、`DEPENDS` 加 `+libjpeg-turbo`，Actions 的 feeds install 加 `libjpeg-turbo`。预期全部 PASS，PR SDK 构建必须证明包名和链接实际可用。
- [ ] **步骤 5：检查 `git diff --check` 并提交。** `git add` 只包含本任务列出的文件；提交 `feat(media): generate bounded reconnect JPEG`。

### 任务 2：独立发布代次与实时／提示帧通道

**文件：** 修改 `src/media_session.h`、`src/media_session.c`、`tests/test_media_session.c`、`tests/test_main.c`。

- [ ] **步骤 1：编写失败的测试。** 一个预览在首帧前没有编码器或提示帧；首帧后设置 `publication_generation == generation`，`frames_received == 1`；把 `media_generation` 改为新尝试值并标记源结束，调用提示帧 tick 后断言编码器 PID／发布代次不变，`frames_received` 与 `last_frame_ms` 不变；随后输入新 JPEG 并断言立即回到 `live`。另测 call purpose 返回不发送提示帧、尺寸变化触发明确错误而不继续旧帧、编码器退出使提示缓存释放。

```c
TEST_ASSERT_INT_EQ((int)session.generation,
    (int)session.publication_generation);
TEST_ASSERT_INT_EQ(1, session.frames_received);
TEST_ASSERT_INT_EQ(DF_MEDIA_SOURCE_RECONNECTING,
    (int)session.source_state);
TEST_ASSERT_INT_EQ(1, session.frames_received);
```

- [ ] **步骤 2：运行 `make test`，预期因新状态或断言失败。**
- [ ] **步骤 3：把发布标识和提示缓存放进私有 session。** 新增 `publication_generation`、`source_state`（`DF_MEDIA_SOURCE_WAITING/LIVE/RECONNECTING`）、`reconnect_frame`、`next_reconnect_frame_ms`。`df_media_session_start_encoder()` 与队列写入使用 `publication_generation`；`media_generation` 仅用于 `df_gvs_monitor_*` admission。首次真实帧将 `publication_generation = generation`，生成提示缓存并启动 FFmpeg；后续真实帧更新 `last_frame_ms` 和 `frames_received`，清除提示节拍但保留一张缓存。仅预览可进入 `RECONNECTING`。

```c
enum df_media_source_state { DF_MEDIA_SOURCE_WAITING,
    DF_MEDIA_SOURCE_LIVE, DF_MEDIA_SOURCE_RECONNECTING };
int df_media_session_tick_reconnect(struct df_media_session *session,
    uint8_t fps, uint64_t now_ms);
int df_media_session_reset_attempt(struct df_media_session *session,
    uint64_t media_generation);
void df_media_session_mark_source_lost(struct df_media_session *session,
    uint64_t now_ms);
```

- [ ] **步骤 4：实现 `tick_reconnect` 和清理。** `mark_source_lost` 在调用时立即切为重连；任务 3 的 manager 在门口机 `03/02` 或静默停帧达到现有 `DF_MEDIA_SESSION_FRAME_STALL_TIMEOUT_MS` 时调用它。间隔为 `1000 / fps` 毫秒，`fps` 的零值由现有配置验证拒绝。先以原 pending 的 generation／timestamp 调用现有 `df_media_session_flush_pending()`，未冲刷完不得构造新 timestamp；之后才调用 `df_media_encoder_write_frame()`。`DF_MEDIA_ENCODER_RETRY` 由 `tick_reconnect` 转为 `DF_OK`，保留单个 pending，不往四槽真实帧队列加入提示帧；时间戳和下一节拍做溢出检查。`reset_attempt` 只重置 `video` 分片和 monitor 相关帧标记，不调用 `stop_pipeline`；`stop_pipeline`、失败、升级来电、`reset` 则释放提示缓存与发布标识。

```c
if (session->purpose != DF_MEDIA_SESSION_PREVIEW ||
    session->publication_generation == 0U ||
    !df_media_encoder_is_running(&session->encoder)) return DF_OK;
int pending = df_media_session_flush_pending(session);
if (pending == DF_MEDIA_ENCODER_RETRY) return DF_OK;
if (pending != DF_OK) return pending;
if (session->source_state == DF_MEDIA_SOURCE_RECONNECTING &&
    now_ms >= session->next_reconnect_frame_ms) {
    struct df_media_frame frame = {
        .data = session->reconnect_frame.data,
        .length = session->reconnect_frame.length,
        .generation = session->publication_generation,
        .timestamp_ms = now_ms,
    };
    int result = df_media_encoder_write_frame(&session->encoder, &frame);
    return result == DF_MEDIA_ENCODER_RETRY ? DF_OK : result;
}
```

- [ ] **步骤 5：运行 `make test && ./build/doorfast-tests`，确认旧 encoder 背压与来电测试仍通过；检查 diff 并提交。** 提交 `feat(media): keep preview publisher across source attempts`。

### 任务 3：门口机重试不再断开 RTSP producer

**文件：** 修改 `src/media_session_manager.c`、`src/media_module.h`、`src/runtime_ubus.c`、`src/runtime_service.c`、`tests/test_media_session_manager.c`、`tests/test_runtime_ubus.c`、`tests/test_runtime_service.c`、`tests/test_main.c`；仅当现有 monitor 重试动作不能实现要求时修改 `src/gvs_monitor.c/.h`、`tests/test_gvs_monitor.c`。

- [ ] **步骤 1：编写失败的控制序列测试。** 复用 `test_media_session_manager_acknowledges_short_preview_and_retries` 的 `03/84`、`03/02 01`、`03/82`、`03/50` fixture；首帧后注入 peer end、忙、再次确认和新帧。断言公开 `generation`、encoder PID、`active_encoders`、`ready`、stream name 均不变，内部 `media_generation` 递增，旧尝试半帧在新尝试前被丢弃，提示帧与真实帧交替；首帧前连续忙仍 `ready=false` 且不启动 FFmpeg。peer end 后第一个 tick 即可送提示，不须额外等 30 秒。

```c
TEST_ASSERT_INT_EQ((int)public_generation,
    (int)manager.sessions[0].generation);
TEST_ASSERT_INT_EQ((int)original_pid,
    (int)manager.sessions[0].encoder.pid);
TEST_ASSERT_INT_EQ(1, status.sessions[0].ready ? 1 : 0);
```

- [ ] **步骤 2：运行 `make test`，预期旧 `reset_pipeline` 路径使 PID／ready 断言失败。**
- [ ] **步骤 3：替换尝试重启路径。** `receive_control()` 的 `result.retrying && peer_stop_seen` 先调用 `df_media_session_mark_source_lost()`；30 秒 stall 路径也调用它。`result.confirmed`、`finish_preview_retry()` 和 stall 路径在已有发布时调用 `df_media_session_reset_attempt(session, session->monitor.generation)`，保留编码器及缓存；状态在已有发布时保持 `PUBLISHING`／`VIEWING`，不得被 `result.confirmed` 又改回 `AWAITING_VIDEO`。`df_media_session_status_ready()` 改以已收到真实首帧、编码器运行和非终止状态为依据，不以当前 `monitor.state` 代替发布状态；状态 fingerprint 纳入 `source_state`，但 ABI 结构不变。内部日志记录 `source_live`／`reconnecting` 转换、真实帧年龄、公开与尝试代次，避免每个 tick 打日志。

```c
if (session->purpose == DF_MEDIA_SESSION_PREVIEW &&
    session->publication_generation != 0U) {
    result = df_media_session_reset_attempt(session,
        session->monitor.generation);
    session->state = session->viewer_active ?
        DF_MEDIA_SESSION_VIEWING : DF_MEDIA_SESSION_PUBLISHING;
}
```

- [ ] **步骤 4：在 manager tick 中驱动提示节拍，仍保留真实 stall 的受控 `03/02`／退避重试。** 明确 stop、来电抢占、配置卸载及 FFmpeg 真退出仍使用完整 `stop_pipeline`／资源释放；提示生成或尺寸不兼容时设置新增的 `DF_MEDIA_ERROR_RECONNECT_FRAME`（追加在 `DF_MEDIA_ERROR_VIDEO_STALLED` 后，现有数值不变），在 ubus／runtime 日志映射为 `reconnect_frame_failed` 并终止该发布，不输出假实时。对应 C 测试检查错误码与映射。

```c
if (df_media_session_tick_reconnect(session, manager->config.fps,
        now_ms) != DF_OK) {
    session->state = DF_MEDIA_SESSION_FAILED;
    session->last_error = DF_MEDIA_ERROR_RECONNECT_FRAME;
    (void)df_media_session_manager_release_failed(manager, session);
    overall = DF_ERR_IO;
}
```
- [ ] **步骤 5：运行 `make test && ./build/doorfast-tests`，逐项检查短流、三台并发、容量不足、来电、stop、encoder exit 和 ABI 状态测试；检查 `git diff --check` 并提交。** 提交 `fix(media): preserve RTSP producer during station retry`。

### 任务 4：端到端模拟和发布门槛

**文件：** 修改 `tests/test_media_session_manager.c`、`tests/run_doorfast_vm_media.py`（只在其现有 fixture 可模拟短流时）；测试记录写入 `docs/superpowers/reports/2026-09-28-preview-continuity-doorfast.md`。

- [ ] **步骤 1：添加失败的生命周期测试。** 三站同时预览、受限容量、双观看者、最后观看者宽限期由 HA 控制后显式 stop、来电抢占、FFmpeg 退出、分辨率变化，分别断言 session／encoder 数、公开 generation 与提示缓存释放；调用 `make test` 看到至少一项失败。

```c
TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_command(&manager,
    DF_MEDIA_MODULE_COMMAND_STOP, &key, false, now_ms));
TEST_ASSERT_INT_EQ(DF_OK, df_media_session_manager_tick(&manager,
    now_ms + DF_GVS_MONITOR_STOP_TIMEOUT_MS));
TEST_ASSERT_INT_EQ(0, (int)df_media_session_manager_active(&manager));
TEST_ASSERT_INT_EQ(0, (int)manager.sessions[0].publication_generation);
TEST_ASSERT_INT_EQ(1, manager.sessions[0].reconnect_frame.data == NULL);
```
- [ ] **步骤 2：统一终止路径。** 在显式 stop ACK／超时、最后观看者的远端 stop、来电抢占、FFmpeg 真退出和 manager destroy 中先调用现有 `df_media_session_manager_stop_resources()`，随后重置 session；`df_media_session_stop_pipeline()` 负责释放提示 JPEG 并清零 `publication_generation`。双观看者只由 HA 发一个共享 viewer 标志，单个观看者关闭不得直接触发这里的 stop。保持原有来电优先级与设备忙退避。

```c
if (df_media_session_manager_stop_resources(session) != DF_OK)
    return DF_ERR_IO;
df_media_session_reset(session);
if (manager->active_count > 0U) manager->active_count--;
```
- [ ] **步骤 3：运行 `make test && ./build/doorfast-tests && git diff --check`。** 报告测试数、构建命令和剩余实机门槛，提交 `test(media): cover preview continuity cleanup`。
- [ ] **步骤 4：仅在 PR Actions 通过后执行可回滚实机验收。** 使用 PR 产物，对 1、2、B2 各十轮真实浏览器 ICE／解码，抓 GVS 控制码、真实帧、提示帧、go2rtc producer 和关闭清理；每台至少九轮 60 秒内有真实帧，主动结束后的同会话自动恢复率至少九成，否则不合并／发布。实机凭据只放环境变量，报告不含 token／密码。

## 单独试验：主动 `03/51`

不在上述生产任务中增加主动心跳。稳定发布通过验收后，若仍要验证短流根因，另建可回滚试验提交：1、2 号各至少五轮 A/B，仅切换 `03/84` 后每两秒主动 `03/51` 并处理 `03/52`；记录规格规定的五项时间／次数。只有两台各至少四轮连续真实视频十秒、各比关闭组多至少两轮且无来电/B2/清理回归，才另立生产修复任务；否则丢弃试验。
