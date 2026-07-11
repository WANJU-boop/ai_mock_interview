# 阶段 1：离线音频设备边界

## 本次目标

新增 `IAudioDevice`、`AudioPcmFormat`、`AudioPcmChunk` 和 `FakeAudioDevice`，先固定
“本地设备只产生/播放 PCM，面试业务不认识 PortAudio”的边界。

## 核心数据流

```text
realtime.audio 配置
  -> AudioPcmFormat
  -> IAudioDevice
       -> tryReadCapturedChunk()：供未来 ASR/WebSocket worker 消费
       -> playPcmChunk()：供未来 TTS PCM 输出播放
```

Fake 按脚本提供录音块并保存播放块，默认测试因此不会申请麦克风/扬声器权限。

## 关键设计

- 采样值使用 `std::int16_t`，表示 PCM S16 的数值而不是网络字节；协议层才处理字节序。
- 捕获读取是 `std::optional` 的非阻塞尝试。没有声音或队列暂空不是异常。
- `stop()` 规定为幂等，后续错误、取消和析构可以安全共享同一清理路径。
- 多声道块必须按完整帧对齐，避免把损坏数据送到 ASR 或扬声器。
- 本阶段仍拒绝真实 provider 的 `input_mod=audio`；接口和配置先落地，真实网络链路留到下一提交。

## 验证

新增单元测试覆盖：接口替换、脚本采集顺序、播放前置条件、立体声半帧拒绝、重复停止，
以及本地音频配置的默认值、显式映射和非正帧数拒绝。

## 下一步建议

下一阶段引入 PortAudio，并将捕获与播放放在受控 worker 中。音频回调只能与有界队列交互，
不能直接调用 WebSocket、LLM 或 UI。
