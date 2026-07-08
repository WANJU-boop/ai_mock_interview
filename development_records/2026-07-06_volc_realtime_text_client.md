# 火山 Realtime 文本模式客户端

## 本次目标

新增火山 Realtime 文本模式客户端，先打通真实 WebSocket transport 的代码边界和离线 fake transport 测试，不把真实网络调用放进默认单元测试。

## 核心数据流

```text
VolcRealtimeClientConfig
  -> VolcRealtimeClient
  -> IVolcRealtimeTransport
  -> BeastVolcRealtimeTransport / FakeVolcRealtimeTransport
  -> VolcRealtimeFrame
```

文本模式流程：

```text
connect WSS
  -> StartConnection
  -> StartSession(input_mod = text)
  -> ChatTextQuery
  -> ChatResponse ... ChatEnded
  -> FinishSession
  -> FinishConnection
```

## 关键 C++ 知识点

- 用 `IVolcRealtimeTransport` 隔离真实 WebSocket，使单元测试不依赖网络和 API key。
- 用 PImpl 隐藏 Boost.Beast WebSocket 类型，避免把重型第三方类型暴露到 public 头文件。
- 真实 WSS transport 保持 TLS 证书校验和 SNI 设置，不提交关闭证书校验的调试代码。
- `VolcRealtimeClient` 只接收已注入的配置；真实密钥由入口层从环境变量读取，避免写进仓库配置。

## 验证结果

已运行：

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

结果：`141/141` 通过。

构建过程中仍出现既有的 macOS/vcpkg 链接警告，但不影响构建和测试结果。

## 下一步建议

下一步把火山事件转换成项目内部 `common::RealtimeEvent`，实现 `IRealtimeClient` 适配层，让 `DialogOrchestrator` 可以在 mock 和火山真实服务之间切换。
