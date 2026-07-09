# Realtime 单一配置入口闭环

## 本次目标

让应用只从一个 JSON 文件读取 Realtime 配置，同时消除
`RealtimeConfig` 与原 `VolcRealtimeClientConfig` 之间重复的供应商默认值。

本次只集中当前文本模式已经实际使用的连接、Dialog 和 TTS 配置。ASR 输入格式和
PortAudio 设备参数尚未被主流程消费，因此没有提前加入无效配置项。

## 核心数据流

```text
config.example.json / config.local.json
  -> loadConfigFromFile
  -> AppConfig.realtime
       -> connection：WSS、环境变量名、资源标识、超时
       -> dialog：模型、输入模式、审核、联网搜索
       -> tts：speaker、PCM 格式、采样率、声道数
  -> resolveVolcRealtimeRuntimeConfig
       -> 从环境变量读取真实 App ID / Access Key
       -> 生成 connect_id / session_id
  -> VolcRealtimeRuntimeConfig
  -> VolcRealtimeClientAdapter 或手动文本 demo
  -> VolcRealtimeClient
```

## 关键设计

- 单一配置源：供应商默认值只存在于 `common::RealtimeConfig` 的子结构中。
- 运行时解析：`VolcRealtimeRuntimeConfig` 不再提供 endpoint、模型或 speaker 默认值，
  防止应用配置和客户端配置长期漂移。
- 密钥隔离：JSON 仍只保存环境变量名，真实密钥只在 resolver 中进入内存。
- 协议边界：WebSocket header 名、火山事件 ID 和运行时追踪 ID 不属于用户配置，
  继续留在客户端/协议实现中。
- 可测试性：factory 和手动 demo 共用同一个 resolver，测试可以离线检查完整字段映射，
  不需要真实网络和火山账号。

## 中文注释自检

- `config.h` 的 connection、dialog、tts public 配置结构都说明了用途和安全边界。
- runtime public API 说明了静态配置、环境变量密钥和运行时 ID 的来源。
- resolver 解释了为什么它是唯一映射点，以及为什么不能把真实密钥写回 JSON。
- 新增测试前均说明了验证目标，以及该边界为什么重要。

## 验证结果

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/AI_mock_interview_realtime_demo config.example.json
```

结果：构建成功，160 项测试全部通过；Realtime mock 使用新的嵌套配置完成 3 道题并
生成报告。链接阶段仍有当前 macOS/vcpkg 工具链已有的 DWARF 和可见性警告，但没有链接失败。

## 下一步建议

下一步进入音频边界时，再新增 `AudioDeviceConfig` 和实际被 ASR 消费的输入格式配置，
并用 fake audio 测试采集、格式转换、发送和 TTS 播放的数据流。不要只增加配置字段而不接主流程。
