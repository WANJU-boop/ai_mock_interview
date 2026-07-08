# Realtime Provider 配置工厂闭环

## 本次目标

把当前分支已有的火山 realtime client、WSS transport 和 adapter 接到应用配置边界上，让应用能通过 `realtime.provider` 选择 mock 或火山 provider。

本次没有接入 PortAudio，也没有宣称完整语音面试已经完成。原因是当前项目还没有麦克风采集和 TTS 播放边界，直接让火山 provider 进入完整 `DialogOrchestrator` 循环会在等待候选人 transcript 时阻塞。

## 核心数据流

mock provider：

```text
config.realtime.provider = "mock"
  -> createRealtimeClient
  -> MockRealtimeClient(scripted_events)
  -> runConfiguredRealtimeInterview
  -> DialogOrchestrator
  -> report JSON
```

火山 provider：

```text
config.realtime.provider = "volc"
  -> createRealtimeClient
  -> 从 VOLC_APP_ID / VOLC_ACCESS_KEY 读取密钥
  -> VolcRealtimeClientAdapter
  -> BeastVolcRealtimeTransport
  -> runRealtimeConnectionSmoke
  -> connect / sendInterviewerText / close
```

## 关键 C++ 知识点

- 工厂函数（factory function）：入口层只依赖 `IRealtimeClient`，具体创建 mock 还是火山 adapter 放在 services 层。
- 依赖注入（dependency injection）：`runConfiguredRealtimeInterview` 接收 `IRealtimeClient&`，方便测试和后续替换真实实现。
- RAII：测试里的 `ScopedEnv` 负责临时设置环境变量，并在析构时恢复，避免污染其他测试。
- 安全配置边界：配置文件只保存环境变量名，不保存真实 App ID、Access Key 或鉴权 header。
- 同步边界说明：`DialogOrchestrator::run()` 仍是同步事件循环，后续接 Qt 时必须放到后台 worker，不能在 UI 线程直接阻塞。

## 注释补充点

- `RealtimeConfig` 解释了为什么默认 provider 是 mock，以及为什么当前阶段只允许 `input_mod=text`。
- `createRealtimeClient` 解释了真实联网发生在 `connect()`，factory 只负责对象组装。
- `runRealtimeConnectionSmoke` 解释了为什么火山 provider 当前只做连接检查，不进入完整面试循环。
- `DialogOrchestratorResult::partial_transcripts` 说明 partial transcript 只给 UI 展示，不参与评分和报告。

## 验证结果

本记录随代码改动一起提交前，需要运行：

```bash
clang-format -i include/common/config.h include/services/realtime_client_factory.h include/app/realtime_demo_app.h include/session/dialog_orchestrator.h src/common/config.cpp src/services/realtime_client_factory.cpp src/app/realtime_demo_app.cpp src/main_realtime_demo.cpp src/session/dialog_orchestrator.cpp test/common/test_config.cpp test/services/test_realtime_client_factory.cpp test/app/test_realtime_demo_app.cpp test/session/test_dialog_orchestrator.cpp
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## 下一步建议

下一步不要直接跳 Qt。优先新建 `codex/audio-device-boundary` 分支，补 `IAudioDevice` / fake audio / PCM 配置，再把火山 `input_mod=audio`、ASR transcript 和 TTS 音频 payload 接进完整 realtime 面试循环。
