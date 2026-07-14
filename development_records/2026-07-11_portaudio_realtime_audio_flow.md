# 阶段 2–3：PortAudio 与 realtime PCM 主链路

## 本次目标

让 `realtime.dialog.input_mod=audio` 能把本地 16-bit PCM 采样安全送进火山 realtime，
并把服务端 TTS PCM 放回扬声器播放队列。默认测试仍使用 fake transport 和 fake audio，
不联网、不申请麦克风权限。

## 核心数据流

```text
PortAudio 输入 callback
  -> PcmSampleQueue（SPSC、有界、无锁）
  -> RealtimeAudioBridge（唯一 WebSocket worker）
  -> IRealtimeClient::sendCandidateAudio
  -> Volc AudioOnlyRequest / raw s16le / sequence
  -> ASRResponse
  -> DialogOrchestrator

火山 TTSResponse / raw s16le
  -> RealtimeAudioBridge
  -> PcmSampleQueue
  -> PortAudio 输出 callback
```

## 并发与资源所有权

- PortAudio callback 只读写自己的 SPSC 队列：不加锁、不分配内存、不访问网络或 UI。
- 调用 `RealtimeAudioBridge` 的同步 `DialogOrchestrator::run()` 是 WebSocket 的唯一 owner，
  发送、读取和关闭不会被多个线程并发调用。
- 正常完成和失败都会先停止音频 callback，再关闭 realtime client；`stop()` 与 `close()` 都可重复调用。
- 当前 CLI 仍在调用线程运行。接入 Qt 时必须把整个 `run()` 放入可 join 的后台 worker，
  再用 queued signal 把 transcript/状态带回主线程；不能从 UI 线程调用同步网络循环。

## 新增依赖

`portaudio` 用于跨平台访问默认输入/输出设备，已同步写入 `vcpkg.json` 与 `CMakeLists.txt`。
可替代库是 miniaudio，但参考目标和已有学习文档使用 PortAudio，因此本阶段不引入第二套音频 API。

## 验证

- PCM SPSC 队列测试覆盖容量、FIFO 回绕、整块拒绝和半块读取拒绝。
- 火山 client/adapter 测试覆盖 audio raw sequence frame、int16 小端编码和 text 模式拒绝音频。
- audio bridge 测试覆盖采集发送、TTS 小端解码、坏 payload 和幂等停止。
- 真实麦克风 + 火山服务属于手动集成检查：需要在 `config.local.json` 设为 `input_mod=audio`，
  注入环境变量后运行 realtime demo；不得写入默认单元测试或提交真实凭据。
