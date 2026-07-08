# Realtime 与 WebSocket 流程

## 两条 realtime 链路

当前项目里有两条 realtime 链路，需要分开记：

1. mock realtime 链路：默认可测试、可离线运行，已经接到面试评分和报告。
2. 火山真实 WSS 链路：手动 smoke test 和协议适配已具备，后续还需要接配置工厂和音频。

## Mock realtime 完整闭环

入口：

```text
src/main_realtime_demo.cpp
```

数据流：

```text
读取 config
  -> createLlmClient
  -> PodofoPdfParser
  -> prepareInterview
  -> buildDefaultRealtimeDemoScript
  -> runRealtimeDemoInterview
  -> MockRealtimeClient
  -> DialogOrchestrator::run
```

`buildDefaultRealtimeDemoScript` 生成固定事件：

```text
kConnected
kTranscriptFinal
kTranscriptFinal
...
```

`DialogOrchestrator` 收到事件后的行为：

```text
kConnected
  -> 发送欢迎语
  -> 发送当前题目
  -> 状态变成 CandidateSpeaking

kTranscriptPartial
  -> 只保留给未来 UI 实时展示
  -> 不评分、不推进题目

kTranscriptFinal
  -> 记录候选人回答
  -> 调用 InterviewManager 评分
  -> 根据分数判断是否追问
  -> 写入 DialogSession
  -> 下一题或结束

kError / kClosed
  -> 状态变成 Error
  -> close realtime client
```

最终输出：

```text
DialogSession
  -> buildInterviewReportJson
  -> Realtime Mock 报告 JSON
```

## 火山真实 WebSocket 文本模式

入口：

```text
src/main_volc_realtime_text_demo.cpp
```

运行前需要环境变量：

```text
VOLC_APP_ID
VOLC_ACCESS_KEY
```

真实调用顺序：

```text
BeastVolcRealtimeTransport
  -> DNS resolve
  -> TCP connect
  -> TLS handshake
  -> WebSocket handshake with auth headers

VolcRealtimeClient
  -> StartConnection
  -> wait ConnectionStarted
  -> StartSession(input_mod = text)
  -> wait SessionStarted
  -> ChatTextQuery
  -> receive ChatResponse ... ChatEnded
  -> FinishSession
  -> FinishConnection
  -> close
```

这里的 `ChatResponse` 是火山对话模型文本响应；当前 demo 直接打印 payload，用来人工确认真实接口可用。

## 火山协议层

核心文件：

```text
include/services/volc_realtime_protocol.h
src/services/volc_realtime_protocol.cpp
```

它做一件事：把结构化的 `VolcRealtimeFrame` 和火山二进制 frame 互相转换。

```text
VolcRealtimeFrame
  -> encodeVolcRealtimeFrame
  -> fixed header
  -> event id / sequence / error code
  -> session id
  -> payload size
  -> payload bytes
```

为什么要单独一层：

- 二进制协议容易出错，必须集中处理长度、字节序和边界。
- payload 可能是 JSON，也可能是 TTS 音频 bytes，协议层不能强行当字符串解析。
- 未知 event id、短 header、payload 长度越界要直接失败，避免坏数据进入业务状态机。

## 火山 client 层

核心文件：

```text
include/services/volc_realtime_client.h
src/services/volc_realtime_client.cpp
```

它不直接碰 Boost.Beast socket，只依赖 `IVolcRealtimeTransport`：

```text
VolcRealtimeClient
  -> IVolcRealtimeTransport::connect
  -> IVolcRealtimeTransport::sendBinary
  -> IVolcRealtimeTransport::receiveBinary
  -> IVolcRealtimeTransport::close
```

这样测试可以注入 fake transport，真实运行再注入 `BeastVolcRealtimeTransport`。

## Boost.Beast WSS transport

核心文件：

```text
include/services/beast_volc_realtime_transport.h
src/services/beast_volc_realtime_transport.cpp
```

它负责真实网络：

- 强制 `wss://`。
- 设置 TLS 证书校验。
- 设置 SNI。
- WebSocket 握手时写入火山鉴权 header。
- 用 binary message 发送火山私有 frame。
- `close()` 尽量幂等，异常路径也能清理资源。

它使用 PImpl，把 Boost.Beast 和 OpenSSL 细节藏在 `.cpp`，避免 public header 变重。

## 适配器层

核心文件：

```text
include/services/volc_realtime_client_adapter.h
src/services/volc_realtime_client_adapter.cpp
```

适配器把火山事件翻译成项目内部事件：

```text
ConnectionStarted / SessionStarted
  -> common::RealtimeEventType::kConnected

AsrResponse + is_interim=true
  -> kTranscriptPartial

AsrResponse + is_interim=false
  -> kTranscriptFinal

ChatResponse
  -> kInterviewerText

DialogCommonError / SessionFailed
  -> kError

SessionFinished / ConnectionFinished
  -> kClosed
```

为什么需要 adapter：

- `session` 层不应该知道火山 event id。
- 业务流程只需要“连接了、候选人最终文本、错误、关闭”这些概念。
- 后续换供应商，只需要新增另一个 adapter，实现同一个 `IRealtimeClient`。

## 关键知识点

- 接口隔离（interface）：用 `IRealtimeClient` 和 `IVolcRealtimeTransport` 切开业务、协议和网络。
- 适配器模式（adapter pattern）：把供应商 frame 映射成项目内部事件。
- PImpl：把 Boost.Beast/OpenSSL 类型隐藏到实现文件。
- RAII 和资源生命周期：transport 持有 socket/SSL context，关闭路径统一收口。
- 二进制协议：固定 header、长度字段、大端序、payload 边界检查。
- JSON 解析：只在明确知道 payload 是 JSON 的事件上解析。
- 状态机：`DialogOrchestrator` 根据 realtime event 推进面试状态。
- 错误处理：异常在 services 内部收口成 `kError` 或 bool 失败，避免穿透业务循环。
- 安全：密钥只从环境变量注入，不打印鉴权 header；真实 WSS 必须校验证书。

## 当前状态和下一步

当前已经完成：

- mock realtime 到评分反馈和报告的闭环。
- 火山二进制协议编解码。
- 火山文本模式 client 和 fake transport 测试。
- 火山 frame 到项目内部 `RealtimeEvent` 的 adapter。

还没有完成：

- 把 `VolcRealtimeClientAdapter` 接到主配置工厂，让 app 可以通过配置选择真实 realtime provider。
- 真实音频输入输出，包括麦克风、PCM chunk、TTS 播放。
- Qt UI 中的实时 transcript 展示和后台线程调度。
