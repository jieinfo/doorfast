# HA 连续音频与全双工卡片实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 subagent-driven-development（推荐）或 executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 从已建立的指定门口机通话连续收听音频，并在原卡片提供一次点击开启/再次点击关闭的免按键全双工模式。

**架构：** 每个 Doorfast 配置项共享一个串行下行拉取器，听众拥有各自的有限游标和连接所有权。认证 WebSocket 使用 start/read/stop 请求，不无限推送；浏览器用原生 Web Audio 播放，麦克风继续复用 PcmProducer 和 AudioWorklet，通过 listener_id 绑定到同一个通话。

**技术栈：** Python asyncio/dataclasses/struct、现有 HA WebSocket、浏览器原生 Web Audio、Node 测试；不新增依赖。

**规格：** [连续音频与免按键双向对讲](../specs/2026-10-12-continuous-duplex-audio-design.md)，用户已确认，规格提交 `f8431b2`。

## 全局约束

- 本计划与规格一起保存在 Doorfast 文档树，但所有下述运行时/测试文件相对 **HA 工作目录**：`/Users/shenwenjie/Documents/PVE/doorfastforha-webrtc-camera`；分支：`codex/integrate-webrtc-camera`。
- HA 基线 `eae9767` / 0.3.25；复用工作树，保护全部既有修改，不 reset、checkout、stash、clean 或重建。
- 先完成 [主程序实现计划](2026-10-12-doorfast-continuous-audio.md)。Python 和 Node 可对 fake v1 接口开发，不依赖实机。
- 下行只附着于已建立的 `talking` 通话，不自动呼叫/接听/挂断/解锁，不改变视频重试与 HA 原状态轮询周期。
- 每项最多四个听众；每连接/每门口机一个；共享缓存最多四块，每块最多 3,200 字节。
- HTTP 真实读取上限 4,096 字节；慢听众积压超过 300 ms 跳到最新，不重播旧语音。
- 听众五秒无读取活动，或连续五秒无有效同身份读取结果，释放资源；304 属于有效读取。
- 浏览器以约 120 ms 缓冲启动，未来播放队列不超过 300 ms；这些不是公网端到端延迟承诺。
- HTTP 页面仅收听；麦克风需要 HTTPS/localhost、用户手势和授权；不安装证书、不改仪表盘。
- 旧 PTT 保持兼容，原上行每批最多五个 320 字节帧、一个请求在途及不重发规则保持。
- 不记录语音、WAV、producer/HA token 或原始媒体信令。临时测试只用合成信号。
- 本计划不授权推送、PR、发布、HACS 更新、实机控制或安装。

## 文件职责

| 文件 | 职责 | 任务 |
| --- | --- | --- |
| `custom_components/doorfast/client_types.py` | 不含 HA 依赖的身份、音频块、读取结果和稳定错误类型 | 1 |
| `custom_components/doorfast/client.py` | 独立游标的有界 v1 HTTP 读取，保留旧 helper | 1 |
| 新 `custom_components/doorfast/audio.py` | WAV 检查、共享 AudioListenerHub、听众游标/失效/缓存 | 1、2 |
| `custom_components/doorfast/websocket.py` | start/read/stop 权限、连接所有权、过期与上下行关联 | 3 |
| `custom_components/doorfast/pcm.py` | 可选 expected runtime 断言，不泄露 producer token | 3 |
| `__init__.py`、`setup_lifecycle.py` | 复用 reconcile/release_entry，集成失败/卸载统一清理 | 3 |
| 新 `frontend/pcm-playback.mjs` | PCM 转换、样本调度和有限 AudioBufferSourceNode 队列 | 4 |
| `frontend/audio-stream.mjs` | 真正下行读取和 epoch/在途请求管理 | 4 |
| `frontend/doorfast-ptt-card.mjs` | ptt/duplex/listen 模式与资源状态机 | 5 |
| 新 `frontend/audio-messages.mjs` | 前端中英文标签、有限错误映射 | 5 |
| `frontend.py`、`frontend/doorfast-pcm-worklet.mjs` | 统一更新静态模块版本，保持注册引用计数 | 5 |
| 新 Python/Node 音频测试、既有 PCM/card/lifecycle 测试 | 对应模块测试和兼容性 | 1–6 |
| `README.md`、`docs/ha-e2e-acceptance-runner.md` | 可用模式、配置和验收限制 | 6 |

顺序 1 → 2 → 3 → 4 → 5 → 6。全部命令在 HA 工作目录执行，除最后跨项目合同检查明确切换目录。

## 公共值类型和 wire 合同

这些是任务 1 要新增的类型，不是已经存在的代码：

```python
@dataclass(frozen=True, slots=True)
class AudioIdentity:
    runtime_id: str
    station_id: str
    generation: int

@dataclass(frozen=True, slots=True)
class AudioChunk:
    identity: AudioIdentity
    previous_revision: int
    revision: int
    pcm: bytes = field(repr=False)
    timestamp_ms: int
    latest_timestamp_ms: int
    dropped_samples: int
    discontinuity: bool

@dataclass(frozen=True, slots=True)
class AudioReadReply:
    state: str  # 只允许 chunk / empty / retry / resync
    chunk: AudioChunk | None = None

class AudioStreamError(RuntimeError):
    def __init__(self, code: str):
        super().__init__(code)
        self.code = code
```

`AudioIdentity.from_status(status,station_id,expected_generation=None)` 也在任务 1 定义：要求 runtime 16 位小写 hex、合法 station、call.session=talking、正 uint64 generation、audio.pcm_stream_version 的实际整数值为 1。不要求 snapshot_ready，允许用户等待首块。拒绝 bool 代替整数、call.station_id 不符和显式 generation 不符。

任务 1 新 `DoorfastClient.read_audio_chunk(identity, after=None)` 使用主程序计划固定的 query、身份头、音频元数据头和稳定 code；不更新 `_audio_generation` / `_audio_revision` / `_audio_etag`。旧 `latest_audio_chunk` 保持原格式及行为。

认证 WebSocket wire 合同：

```json
{"type":"doorfast/audio/listen_start","config_entry_id":"one","station_id":"gate_main"}
```

```json
{"listener_id":"opaque","station_id":"gate_main","generation":7,"sample_rate":8000,"channels":1,"sample_format":"s16le"}
```

读取携带 entry/listener；结果为 `{"state":"empty"}` 或以下结构，PCM base64 只存在于响应，不存进实体状态：

```json
{"state":"chunk","listener_id":"opaque","generation":7,"previous_revision":30,"revision":40,"pcm":"AAD/fw==","discontinuity":false}
```

示例 PCM 是四字节合成测试样本，不是真实录音。新路径 WebSocket 数字必须在 JS 安全整数范围内；超范围身份明确拒绝，不能无声舍入。

### 任务 1：独立身份读取与有界 WAV 解包

**文件：** `client_types.py`、`client.py`、新 `audio.py`、新 `tests/test_audio_client.py`、`tests/test_client.py`。

- [ ] **步骤 1：写失败测试。** 使用现有动态 loader 模式和独立 fake aiohttp 流，避免与其他测试的全局 HA stub 冲突。新 fake 实现 `response.content.read(n)`，可以逐段返回真实字节，不能只给一个无限 `.read()` 来掩盖上限。首个测试读取 runtime/station/generation 都正确的 100 ms WAV，断言 query 身份/独立 after、PCM 恰好 1,600 字节、旧 client 游标未变。

```python
def synthetic_wav(samples=800):
    pcm = bytes(samples * 2)
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(pcm), b"WAVE",
        b"fmt ", 16, 1, 1, 8000, 16000, 2, 16, b"data", len(pcm),
    ) + pcm

class AudioClientTest(unittest.IsolatedAsyncioTestCase):
    async def test_v1_chunk_does_not_consume_legacy_cursor(self):
        identity = AudioIdentity("0123456789abcdef", "gate_main", 7)
        client = make_audio_client(synthetic_wav(), identity, 30, 40)
        reply = await client.read_audio_chunk(identity, after=30)
        self.assertEqual("chunk", reply.state)
        self.assertEqual(bytes(1600), reply.chunk.pcm)
        self.assertIsNone(client._audio_revision)
```

`make_audio_client` 在新测试定义，复用 FakeSession 的请求记录形状，构造所有主程序合同头；测试既传参数又核实请求记录。另测 304 身份头、同身份 404、503、cursor-expired resync、identity-mismatch 必须终止、错误/缺失 header、revision逆序/不连贯、runtime 改变且 generation 相同、宣称小 Content-Length 但实际超限、分块 HTTP 正文、错采样率/声道/位宽、奇数 PCM、截断 RIFF、额外尾字节和空/超大 data。

- [ ] **步骤 2：运行红色测试。** `python3 -m unittest discover -s tests -p test_audio_client.py -v`；预期新值类型和方法不存在，确认 loader 可工作后再判断行为失败。
- [ ] **步骤 3：实现。** 类型和身份校验放 client_types；`decode_pcm_wav(body)` 放 audio，按 RIFF chunk 边界和偶数 padding 检查 fmt/data，只接受 PCM1/mono/8000Hz/16bit/block_align2/byte_rate16000、正文偶数且 ≤3,200。新 helper 在 HTTP 2 秒超时内增量读取，硬上限 4,096；非 200 也有界读取 JSON。unknown/malformed code 拒绝，304 必须核对身份；404 同身份且 audio_unavailable → empty，503 → retry，只有明确 cursor-expired → resync。丢弃量和 timestamp 都验证 uint64，timestamp≤latest；旧 helper 不套用新的小块/WAV限制。

```python
body = bytearray()
while True:
    part = await response.content.read(min(1024, 4097 - len(body)))
    if not part:
        break
    body.extend(part)
    if len(body) > 4096:
        raise AudioStreamError("audio_invalid_response")
```

- [ ] **步骤 4：验证绿及兼容。** 运行新测试与 `python3 -m unittest discover -s tests -p test_client.py -v`；确保旧 fake WAV 测试和 revision 恢复行为不变。
- [ ] **步骤 5：提交精确文件。** `git add custom_components/doorfast/client_types.py custom_components/doorfast/client.py custom_components/doorfast/audio.py tests/test_audio_client.py tests/test_client.py && git commit -m "feat(audio): validate bounded station PCM downlink reads"`。

### 任务 2：共享拉取、独立听众游标与有限缓存

**文件：** `audio.py`、新 `tests/test_audio_listener.py`。

- [ ] **步骤 1：写失败测试。** 本任务定义 `AudioListenerHub(client,identity,clock)`；`clock` 是返回单调秒数的函数，测试用可推进 fake，不执行真实 sleep。

```python
class AudioListenerTest(unittest.IsolatedAsyncioTestCase):
    async def test_two_listeners_receive_same_chunk_without_two_fetches(self):
        now = [0.0]
        client = FakeAudioClient([chunk_reply(0, 1)])
        hub = AudioListenerHub(client, IDENTITY, lambda: now[0])
        hub.add("first")
        hub.add("second")
        a = await hub.read("first")
        b = await hub.read("second")
        self.assertEqual(a.chunk.pcm, b.chunk.pcm)
        self.assertEqual(1, client.read_count)
```

新测试自行定义 `IDENTITY`、FakeAudioClient（read_count、异步 gate、reply队列）和 chunk_reply（800样本、合法身份/计数/时间）。覆盖同时读取、304不重播、四块/四听众上限、第五听众拒绝、300 ms积压、源端 timestamp lag、cursor过期、五秒失败截止、仅304不判失败、有效回复重置失败截止、identity变化、close中迟到fetch、去除最后听众后的零请求。

- [ ] **步骤 2：运行红色测试。** `python3 -m unittest discover -s tests -p test_audio_listener.py -v`。
- [ ] **步骤 3：实现。** add/remove/read/close 分别管理 opaque ID 的游标和缓存；read 用每听众 lock，再取得配置项 fetch lock。缓存 deque 最大四块，单调本地获取时间和源端 lag共同防积压。已有尚未消费块直接返回；两个同时请求只允许一次共享 fetch。收到空结果做最多50 ms 的拉取冷却，避免两个浏览器串行空请求翻倍；等待 lock 后再检查 epoch、缓存和冷却。

| 方法 | 实际职责 |
| --- | --- |
| `add(listener_id: str) -> None` | 检查四听众上限，建立未读游标和每听众锁 |
| `remove(listener_id: str) -> None` | 删除该听众；删除最后听众时立即关闭 hub |
| `async read(listener_id: str) -> AudioReadReply` | 串行取得缓存或新回复，验证 epoch 并推进此听众游标 |
| `close() -> None` | 同步使全部在途读取失效并清空所有有限状态 |

按 client 结果更新共享 cursor；resync 清空旧缓存/游标并使下一新块带 discontinuity，不能重复返回已消费 revision。`close` 同步递增 epoch、清空缓存/听众，迟到网络结果不得写回。返回给听众的 block不可修改共享对象；慢听众仅移动自身游标。临时失败在五秒 deadline 内返回 retry；超过 deadline 抛 `audio_connection_lost`；identity失配立即抛错，不恢复到别台/别代。

- [ ] **步骤 4：验证绿。** 跑新测试，并用 async gate 证明 bridge 最大在途数为一；在已关闭 hub 完成 gate，断言缓存仍为空且没有新请求。
- [ ] **步骤 5：提交。** `git add custom_components/doorfast/audio.py tests/test_audio_listener.py && git commit -m "feat(audio): share bounded downlink across independent listeners"`。

### 任务 3：认证 WebSocket、过期清理和同通话麦克风绑定

**文件：** `websocket.py`、`pcm.py`、`__init__.py`、`setup_lifecycle.py`、新 `tests/test_audio_websocket.py`、`tests/test_pcm.py`、`tests/test_pcm_websocket.py`、`tests/test_setup_lifecycle.py`。

- [ ] **步骤 1：写失败测试。** manager 新增 `listen_start/listen_read/listen_stop`，注册三个命令。新增 listener记录包含 entry、connection、listener_id、AudioIdentity、hub、lease timer、active read state。为 start/read/stop 增加 schema并验证所有权，不把 producer token 返回给浏览器。

```python
await manager.listen_start(connection, {
    "id": 1, "config_entry_id": "one", "station_id": "gate_main",
})
listener_id = connection.results[-1][1]["listener_id"]
await manager.start(connection, {
    "id": 2, "config_entry_id": "one", "station_id": "gate_main",
    "listener_id": listener_id,
})
```

使用 `tests/test_pcm_websocket.py` 的 Connection/FakeHass 结构，但新的 fake client 提供 refresh、v1读取和 PCM primitive。覆盖非管理员、跨连接/entry、重复start、start与stop竞态、四听众上限、另一麦克风占用、生成号大于JS安全范围、unload/rollback/reconcile/断线/五秒idle、下行失效联动麦克风、先释放ID再await网络、晚到的producer-open必须关闭。PcmProducer 新测试提供 `expected_runtime_id`，在 refresh 与 open 前发现重启复用同generation时必须零open。

- [ ] **步骤 2：运行红色测试。** `python3 -m unittest discover -s tests -p test_audio_websocket.py -v` 和对应 PCM 定向测试。
- [ ] **步骤 3：实现生产接线。** 沿用现有 websocket decorator、async_response、connection cleanup 与 release_entry；manager按entry共享hub，start先fresh status绑定identity，能力不符返回 `unsupported_audio_stream`。当前 entry 的 hub 身份与新通话不符时，先使旧 hub 和关联 capture 失效，再创建新 hub；不能把旧听众移到新通话。cleanup回调从connection.subscriptions删除，避免长期reload积累。lease使用 asyncio loop.call_later(5.0, expire_callback)，每次读取活动重置，release取消timer。expire_callback 在本模块定义，只调度原listener的幂等释放。

```python
# PcmProducer.__init__ 新增 keyword-only 参数。
def __init__(self, client, station_id=None, generation=None,
             *, expected_runtime_id=None):
    self._expected_runtime_id = expected_runtime_id
```

本代码段仅展示新签名与字段；原构造器全部原初始化必须保留。start及每次recover刷新后，若expected runtime存在则精确匹配，不能重新打开替代runtime。manager给duplex传入listener的runtime/station/generation；带listener_id不能退回忽略身份的旧factory TypeError兼容分支。producer.start结束后，再验证listener尚存在且同identity/connection，才发布capture result，否则stop该producer。

`_Capture` 增加可选 listener_id/runtime_id；reconcile 和下行失效只释放同身份的关联capture。stop监听不物理挂断；legacy不带listener_id仍保持原PTT流程。`release_entry` 同步使全部hub失效并移除listeners/captures，然后最佳努力关闭producer；调用者无需新增一套卸载流程。

- [ ] **步骤 4：验证绿。** 运行新测试、原 PCM/WS/lifecycle 三组。注册命令计数从3更新为6，并验证幂等。模拟Connection的真实subscriptions字典cleanup及fallback回调，两者都验证无残留；不运行会修改真实HA的acceptance runner。
- [ ] **步骤 5：提交。** 精确暂存本任务实际文件，提交 `feat(audio): bind authenticated listeners and microphone lifecycles`。

### 任务 4：原生 PCM 输出与安全轮询

**文件：** 新 `frontend/pcm-playback.mjs`、`frontend/audio-stream.mjs`、新 `tests/js/test_pcm_playback.mjs`、`tests/js/test_audio_stream.mjs`。

- [ ] **步骤 1：写失败测试。** 新模块导出 `pcm16ToFloat32(buffer)`、`PcmAudioOutput(context,{startupBufferMs=120,maxBufferMs=300})`，后者有 enqueue(buffer,{discontinuity=false}) 和 stop()。FakeAudioContext只实现currentTime/createBuffer/createBufferSource/destination，每个source记录start/stop/connect；不用真实声卡。

```javascript
test("PCM output schedules sample-clock audio and stops all nodes", () => {
  const context = new FakeAudioContext(2);
  const output = new PcmAudioOutput(context);
  output.enqueue(new Uint8Array(1600).buffer);
  assert.ok(Math.abs(context.sources[0].startAt - 2.12) <= 1e-9);
  context.currentTime = 2.05;
  output.enqueue(new Uint8Array(1600).buffer);
  assert.ok(Math.abs(context.sources[1].startAt - 2.22) <= 1e-9);
  output.stop();
  assert.ok(context.sources.every((source) => source.stopped));
});
```

FakeAudioContext定义在此新测试文件；node fake支持onended模拟和disconnect。浮点时间断言使用误差≤1e-9，不依赖十进制浮点完全相等。覆盖-32768/32767转换、字节大小/奇数拒绝、最大块、44.1/48k输出context、300ms积压、underflow和discontinuity重置、stop幂等、end节点释放。

原PcmAudioPlayback增加测试：同一start最多一个fetch、stop期间fetch/输出错误晚到不能停止新start、空块不播放、stop后无timer；使用可控promise和timer，不依赖5ms sleep可靠性。

- [ ] **步骤 2：运行红色测试。** `node --test tests/js/test_pcm_playback.mjs tests/js/test_audio_stream.mjs`。
- [ ] **步骤 3：实现。** PCM长度1–3,200且偶数，用DataView.getInt16(offset,true)/32768转换；createBuffer(1,samples,8000)，连续按samples/8000推进nextStart，不用HTML audio或下行worklet。未来队列超300ms/不连续时stop旧未播放节点，重置短缓冲；缺块自然静音，恢复重排时间基准。

```javascript
const startAt = Math.max(this.nextStart ?? 0,
  this.context.currentTime + this.startupBufferMs / 1000);
```

上式只用于启动/underflow重建；正常连续块必须沿用nextStart，不能每块都增加120ms。PcmAudioPlayback接入 `listen_read`，空结果50ms后读，拿到块可立即读下一块但不得并行；epoch不符不能调用stop作用到新epoch。默认原playChunk注入兼容，真实模式传入PcmAudioOutput。

- [ ] **步骤 4：验证绿。** 跑新Node测试及所有既有DSP/batchsender测试；保持上行max5帧和单请求不变。
- [ ] **步骤 5：提交。** `git add custom_components/doorfast/frontend/pcm-playback.mjs custom_components/doorfast/frontend/audio-stream.mjs tests/js/test_pcm_playback.mjs tests/js/test_audio_stream.mjs && git commit -m "feat(audio): play bounded PCM with browser sample-clock scheduling"`。

### 任务 5：卡片模式、权限反馈和资源缓存版本

**文件：** `frontend/doorfast-ptt-card.mjs`、新 `frontend/audio-messages.mjs`、`frontend/doorfast-pcm-worklet.mjs`、`frontend.py`、`tests/js/test_ptt_card.mjs`、新 `tests/js/test_audio_messages.mjs`、`tests/test_frontend.py`。

- [ ] **步骤 1：写失败测试。** 卡片config增加mode，缺省ptt；duplex/listen必需合法station，generation可选。fakeDOM补齐addEventListener等实际卡片操作。用一次click触发持续采集，再pointerup断言仍active；第二click释放。保留原pointerdown/up/Space/Enter和press-target不重建测试。

```javascript
card.setConfig({
  config_entry_id: "one", station_id: "gate_main", mode: "duplex",
});
await card._toggle();
assert.equal(card._status, "active");
await card._toggle();
assert.equal(card._status, "idle");
```

`_toggle()` 是本任务新增方法；测试fakeWS返回新listen/capture合同，fakecontext/track/worklet均记录资源关闭。覆盖HTTP duplex仅收听且getUserMedia零调用、用户拒绝但继续收听、starting时再次点击取消、任一步错误释放已经取得的资源、late权限track关闭、late下行ID释放、pending旧stop不能释放新会话、失焦/隐藏/断线/配置改变/track ended清理和自动重连不重启麦克风。

- [ ] **步骤 2：运行红色测试。** `node --test tests/js/test_ptt_card.mjs`；前端注册版本测试先改期望为新版本，再运行 `python3 -m unittest discover -s tests -p test_frontend.py -v`，确认旧资源 URL 真正失败。
- [ ] **步骤 3：实现。** 用户点击时同步创建/resumeAudioContext；监听start后创建真正输出和轮询。duplex安全上下文中并行获取mic与worklet，但必须等待权限promise收尾，释放late track；麦克风start使用刚取得listener_id绑定，再连接现有worklet/sender。HTTP不请求mic，显示“仅收听：麦克风需要HTTPS”；权限NotAllowedError明确显示仅收听，不伪装双向active。

```javascript
const mode = config.mode ?? "ptt";
if (!["ptt", "duplex", "listen"].includes(mode)) throw new Error("Invalid audio mode");
```

stop先递增epoch、停止输出节点/timer/worklet/track，再对原entry、原listener/capture最佳努力请求backendstop；共享context由卡片关闭一次。设置status只更新已存在的button，不能重建持有pointercapture的DOM。setAudioPlayback注入钩子仍兼容原测试，但生产模式无外部注入也能发声。

`audio-messages.mjs` 导出 `audioMessage(language,key)`，至少含 Listen/Stop listening、Start intercom/Stop intercom、Listening only、HTTPS required、Microphone denied、Call not established、Station mismatch、Microphone busy、Update Doorfast、Audio connection lost 的英文/简体中文；语言来自hass.locale.language或hass.language，未识别语言fallback英文，不显示原始后端异常正文。

前端独立版本 `FRONTEND_ASSET_VERSION="20261012.1"` 放frontend.py；根卡片URL、音频模块依赖URL、workletURL和worklet DSP依赖URL均用这一版本字串。测试读取实际JS导入并核实一致；不修改manifest/release版本，也不为缓存修复改SW_VERSION设备实体元数据。

- [ ] **步骤 4：验证绿。** `node --test tests/js/*.mjs` 与frontend注册测试；检查旧卡片没station仍可PTT，新模式不以缺失generation误操作全局通话。
- [ ] **步骤 5：提交。** 精确暂存本任务文件，提交 `feat(audio): add explicit duplex and listen card modes`。

### 任务 6：集中故障测试、文档和跨项目验收准备

**文件：** 新 `tests/test_audio_contract.py`、新 `tests/js/test_duplex_lifecycle.mjs`；修改 `README.md`、`docs/ha-e2e-acceptance-runner.md`，缺陷修复仅涉及任务1–5。

- [ ] **步骤 1：增加合成端到端合同测试。** Python用真实AudioIdentity/HTTP解析/hub/manager + fake响应流，依次模拟first chunk→304→503→cursor expired→same identity resync→runtime changed with generation7→cleanup；两个listener均可独立消费第一块，最后无hub/capture/timer残留。测试fake记录PCM摘要和请求数，不输出音频正文。Node从真实卡片经fakecallWS返回PCM，经真实PcmAudioOutput断言节点创建与清理，不能只用setAudioPlayback noop代替播放器验收。

```python
self.assertEqual("chunk", first["state"])
self.assertEqual("empty", unchanged["state"])
self.assertEqual("audio_identity_mismatch", final_error.code)
self.assertEqual({}, manager._listeners)
self.assertEqual({}, manager._captures)
```

`manager._listeners` 是任务3定义的ID映射；本任务测试构造一条完整链路产生first/unchanged/final_error，而非静态字典。generation隔离测试做一次临时mutation移除expected runtime guard，确认失败后立即恢复并重跑。

- [ ] **步骤 2：全量验证。** 不更新版本、不运行真实HA控制runner：

```sh
set -e
python3 -m compileall -q custom_components/doorfast
python3 -m unittest discover -s tests
node --test tests/js/*.mjs
python3 -c 'import json,pathlib; [json.loads(p.read_text()) for p in pathlib.Path("custom_components/doorfast").rglob("*.json")]'
git diff --check
```

- [ ] **步骤 3：更新用户文档与审查。** README提供三种mode和station配置实例、已经talking的前提、停止音频≠挂断、HTTP仅收听、拒绝权限、不能自动重启mic；去除“没有live player”的过时说明。验收文档明确fixture/localhost/HTTPS/现场扬声器证据层级；部署步骤仍由用户授权。独立审查聚焦重启复用generation、producer token隐私、所有权、async取消、并发read/stop、timer/cleanup泄漏、前端缓存及真实播放而非noop。

```yaml
type: custom:doorfast-ptt-card
config_entry_id: one
station_id: gate_main
mode: duplex
name: Main gate intercom
```

此配置实例用于文档/fixture；现场用户必须使用自己的entry/station，不自动写入仪表盘。

- [ ] **步骤 4：合同核对与提交。** 对照主程序计划确认header/query/code/PCM尺寸一致；运行主程序全量验证清单。精确提交实际文件，提交信息 `test(audio): cover duplex lifecycle and v1 compatibility`。汇报两个仓库本地SHA、测试退出码和未完成现场项，不声称全双工已实机通过。

## 自检覆盖索引

HTTP/WAV/身份/旧主程序提示 → 任务1、3；听众/游标/有限缓存/失败截止 → 任务2；权限/连接/上行关联/卸载/过期 → 任务3；真实输出/样本时钟/epoch → 任务4；三模式/HTTP/拒绝/语言/缓存版本 → 任务5；组合竞态/原功能回归/无录音日志/现场边界 → 任务6。
