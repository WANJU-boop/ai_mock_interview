# 项目术语表

这份术语表只解释本项目里经常出现、容易混淆的概念。英文原词保留在括号里，方便你回到代码里搜索。

| 术语 | 含义 | 在代码里主要出现在哪里 |
| --- | --- | --- |
| 面试会话（DialogSession） | 保存一次面试运行中的状态、候选人回答、评分结果和结构化问答记录。它像一份正在填写的面试过程表。 | `include/session/dialog_session.h` |
| 面试状态（InterviewState） | 表示流程当前处于连接中、面试官提问、候选人回答、面试官思考、结束、完成或错误。新手可以把它理解成“状态机的枚举值”。 | `include/session/interview_state.h` |
| 面试管理器（InterviewManager） | 管理题目列表和当前题目，负责记录回答、调用 LLM 评分、判断是否追问、推进到下一题。 | `include/session/interview_manager.h` |
| 准备好的面试（PreparedInterview） | 启动阶段准备出的面试上下文，里面包含候选人信息、目标岗位和可运行的 `InterviewManager`。失败时也能保存错误信息。 | `include/session/interview_setup.h` |
| 问答记录（QuestionAnswerRecord） | 一道题的完整记录：主问题、主回答、是否追问、追问文本、追问回答、最终评分。 | `include/session/dialog_session.h` |
| 评分结果（LlmScoreResult） | LLM 或 mock 返回的评分结构，包含 `score` 和 `feedback`。 | `include/services/llm_client.h` |
| 追问决策（FollowUpDecision） | 说明当前回答是否需要追问，以及追问文本是什么。项目里用分数段决定是否追问。 | `include/session/interview_manager.h` |
| LLM 客户端接口（ILlmClient） | 项目需要大模型完成的能力抽象：生成问题、给回答评分。业务层只依赖这个接口，不关心是真模型还是 mock。 | `include/services/llm_client.h` |
| Mock LLM（MockLlmClient） | 不联网的假 LLM，返回稳定问题和评分，用于测试和学习闭环。 | `include/services/llm_client.h`、`src/services/llm_client.cpp` |
| HTTP LLM（HttpLlmClient） | 把项目里的生成问题/评分请求转换成 OpenAI 兼容 HTTP JSON 请求，再解析响应。 | `include/services/http_llm_client.h` |
| HTTP 传输接口（IHttpTransport） | 只负责发送 HTTP JSON 请求的抽象。这样 `HttpLlmClient` 可以用 fake transport 离线测试。 | `include/services/http_llm_client.h` |
| Beast HTTP 传输（BeastHttpTransport） | 基于 Boost.Beast 和 OpenSSL 的真实 HTTPS POST 实现。 | `include/services/beast_http_transport.h` |
| PDF parser 接口（IPdfParser） | 简历 PDF 文本解析能力的抽象。测试可以用 mock，真实运行可以用 PoDoFo。 | `include/services/pdf_parser.h` |
| PoDoFo PDF parser（PodofoPdfParser） | 使用 PoDoFo 解析文本型 PDF 简历的真实实现。 | `include/services/pdf_parser.h`、`src/services/pdf_parser.cpp` |
| Realtime 事件（RealtimeEvent） | 项目内部统一理解的实时事件，比如已连接、临时转写、最终转写、错误、关闭。 | `include/common/realtime_protocol.h` |
| Realtime 客户端接口（IRealtimeClient） | realtime 服务能力抽象，包含连接、收事件、发面试官文本、关闭。`session` 层只依赖这个接口。 | `include/services/realtime_client.h` |
| Mock Realtime（MockRealtimeClient） | 用固定脚本吐出 realtime 事件的假客户端，不需要网络、麦克风或服务账号。 | `include/services/realtime_client.h` |
| 对话编排器（DialogOrchestrator） | 把 realtime 事件流接到面试领域逻辑：收到连接就提问，收到最终转写就评分，结束后生成报告。 | `include/session/dialog_orchestrator.h` |
| 火山 Realtime frame（VolcRealtimeFrame） | 火山供应商 WebSocket 私有二进制协议的一帧。它不是项目内部事件，而是供应商协议细节。 | `include/services/volc_realtime_protocol.h` |
| 火山 Realtime client（VolcRealtimeClient） | 负责火山文本模式协议流程：WSS 连接后发送 StartConnection、StartSession、ChatTextQuery、Finish。 | `include/services/volc_realtime_client.h` |
| 火山 transport（IVolcRealtimeTransport） | 火山 WebSocket 底层传输抽象，只负责 connect、sendBinary、receiveBinary、close。 | `include/services/volc_realtime_client.h` |
| Beast WSS transport（BeastVolcRealtimeTransport） | 基于 Boost.Beast 的真实 WSS 传输实现，处理 TLS、SNI、WebSocket handshake 和 binary message。 | `include/services/beast_volc_realtime_transport.h` |
| 火山适配器（VolcRealtimeClientAdapter） | 把火山 frame 翻译成项目内部 `RealtimeEvent`，让业务层不直接认识火山 event id。 | `include/services/volc_realtime_client_adapter.h` |
| 协议边界（protocol boundary） | 把二进制数据和业务结构分开的地方。项目里有内部 realtime frame 和火山 frame 两种协议边界。 | `src/common/realtime_protocol.cpp`、`src/services/volc_realtime_protocol.cpp` |
| 依赖注入（dependency injection） | 构造对象时把接口实现传进去，例如给 `InterviewManager` 传 `ILlmClient&`，方便测试替换成 mock。 | `include/session/interview_manager.h` |
| 适配器模式（adapter pattern） | 把外部接口转换成项目内部接口。这里最典型的是 `VolcRealtimeClientAdapter`。 | `include/services/volc_realtime_client_adapter.h` |
| PImpl | 把复杂第三方库类型藏到 `.cpp` 里的写法，减少 public 头文件依赖。 | `include/services/beast_volc_realtime_transport.h` |
| 手动集成测试（manual integration check） | 需要真实账号、网络或外部环境的检查，不放进默认 `ctest`。 | `src/main_volc_realtime_text_demo.cpp` |

## 新手记忆方式

可以把项目里的术语按三类记：

```text
业务词：面试会话、题目、回答、评分、追问、报告
接口词：ILlmClient、IPdfParser、IRealtimeClient、IHttpTransport、IVolcRealtimeTransport
供应商词：VolcRealtimeFrame、StartConnection、StartSession、ChatTextQuery、ChatTTSText
```

业务词属于 `session`，接口词主要属于 `services`，供应商词只应该停留在 `services/volc_*`。
