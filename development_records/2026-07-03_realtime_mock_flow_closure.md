# Realtime mock 流程闭环

## 本次目标

完成进入真实 WebSocket 之前的 1-4 阶段：

1. 建立 realtime 协议和事件 frame 边界。
2. 建立 `IRealtimeClient` 与 `MockRealtimeClient`。
3. 建立 `DialogOrchestrator`，把 realtime 事件接入现有面试流程。
4. 建立可运行的 `AI_mock_interview_realtime_demo`，用 mock realtime 脚本跑通完整流程。

这样做的原因是：真实 WebSocket 会引入网络、鉴权、TLS、线程和关闭顺序问题。如果在没有纯数据边界和确定性 mock 的情况下直接接服务端，错误会很难定位。现在先把事件、状态、评分、追问和报告闭环固定下来，下一步只需要把 `IRealtimeClient` 的实现替换成真实 WebSocket 客户端。

## 核心数据流

1. `main_realtime_demo` 读取 `config.example.json` 或外部配置。
2. `prepareInterview` 复用现有 LLM/PDF 启动链路，生成题目和 `InterviewManager`。
3. `buildDefaultRealtimeDemoScript` 生成 mock realtime 事件：先 `Connected`，再为每道题提供 `TranscriptFinal`。
4. `DialogOrchestrator` 收到 `Connected` 后发送欢迎语和当前题目。
5. `MockRealtimeClient` 按脚本吐出候选人 final transcript。
6. `DialogOrchestrator` 调用 `InterviewManager` 记录回答、评分、决定是否追问。
7. 每道题完成后写入 `DialogSession` 的结构化问答记录。
8. 所有题目完成后进入 `Completed`，输出 `buildInterviewReportJson` 生成的报告。

## 关键 C++ 知识点

- 接口隔离：`IRealtimeClient` 是后续真实 WebSocket 的替换点，`session` 层不依赖 Boost.Beast。
- 纯数据协议：`RealtimeEvent` 和 frame 编解码在 `common` 层，方便 mock、WebSocket 和 UI 共同使用。
- 确定性测试：`MockRealtimeClient` 用固定事件脚本覆盖连接、partial transcript、final transcript、错误和关闭。
- 编排层职责：`DialogOrchestrator` 只协调状态、事件、评分和报告，不处理 socket 读写细节。
- 错误收口：realtime error、提前关闭、事件流耗尽都会进入 `InterviewState::kError`，不会生成半成品成功报告。

## 注释自检

- 新增 public API 已补中文注释：`RealtimeEvent`、`IRealtimeClient`、`MockRealtimeClient`、`DialogOrchestrator`、demo app 入口。
- 新增测试用例前已说明验证目标和边界价值。
- 非平凡逻辑已补中文注释：frame 长度校验、mock close 行为、partial transcript 不推进评分、demo 默认脚本目的。

## 验证结果

- 已运行 `clang-format -i` 格式化新增 C++ 头文件、源文件和测试文件。
- `cmake --build build -j` 通过；构建过程中需要提升权限访问 vcpkg 根目录锁文件。链接阶段仍有既有 duplicate libraries warning，不影响目标生成。
- `ctest --test-dir build --output-on-failure` 通过，128/128。
- 已手动运行：

```bash
./build/AI_mock_interview_realtime_demo config.example.json
```

运行结果：生成 3 道题，mock realtime 完成回答，输出 `Realtime Mock 报告 JSON`，进程返回 0。

## 下一步建议

下一步可以开始真实 WebSocket：

1. 新增 `WebSocketRealtimeClient final : public IRealtimeClient`。
2. 构造函数接收 endpoint、鉴权信息、超时和 TLS 开关配置，但不能把 token 打进日志。
3. 内部先用同步或单线程 Boost.Beast 版本跑通 `connect -> receiveNextEvent -> sendInterviewerText -> close`。
4. 保持默认单元测试继续使用 `MockRealtimeClient`；真实服务只做手动集成测试。
5. 真实客户端稳定后，再考虑音频 `IAudioDevice` 和 Qt UI，不要让 Qt 直接读写 WebSocket。
