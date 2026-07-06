# 火山 Realtime 适配到项目接口

## 本次目标

新增 `VolcRealtimeClientAdapter`，把火山供应商 frame 映射成项目内部 `common::RealtimeEvent`，让 session 层继续只依赖 `IRealtimeClient`，不直接认识火山 event id 或 payload JSON。

## 核心数据流

```text
VolcRealtimeFrame
  -> mapVolcRealtimeFrameToRealtimeEvent
  -> common::RealtimeEvent
  -> IRealtimeClient
  -> DialogOrchestrator
```

adapter 启动时会完成：

```text
connect
  -> StartConnection / ConnectionStarted
  -> StartSession / SessionStarted
  -> 暴露 common::RealtimeEventType::kConnected
```

## 关键 C++ 知识点

- 使用 adapter pattern 把供应商协议和项目业务事件隔离。
- 使用 `std::optional` 表达“这个火山事件不需要暴露给业务层”。
- 对 JSON payload 解析失败统一转为 `kError`，避免异常穿透到 session 状态机。
- `sendInterviewerText` 通过 ChatTTSText 发送文本合成请求，保持现有 `IRealtimeClient` 接口不变。

## 验证结果

已运行：

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

结果：构建通过，全量测试 `146/146` 通过。

## 下一步建议

后续可以继续补配置工厂，把 `provider = volcengine` 接到应用入口；真正语音输入输出仍建议放到音频阶段，新增 `IAudioDevice` / PortAudio 后再接 `TaskRequest` 和 TTS 播放。
