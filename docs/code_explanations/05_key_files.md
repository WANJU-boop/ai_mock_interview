# 最重要的 5 个文件

这 5 个文件适合作为新手读项目的入口。它们不覆盖全部实现，但能帮你抓住项目主线。

| 文件 | 一句话职责 |
| --- | --- |
| `src/main.cpp` | 主 CLI 程序入口，负责初始化日志、读取配置、创建 LLM/PDF 依赖、准备面试，然后把流程交给 `runCliInterview`。 |
| `include/session/interview_manager.h` | 定义面试核心管理器，负责当前题目、回答记录、评分调用、追问决策和题目推进。 |
| `src/app/cli_interview_app.cpp` | 实现终端文字面试闭环，把输入回答、评分反馈、追问、总结和报告 JSON 串起来。 |
| `include/services/llm_client.h` | 定义项目使用大模型的稳定接口 `ILlmClient`，让业务层不用关心 mock、HTTP 或未来其他 provider。 |
| `src/session/dialog_orchestrator.cpp` | 实现 realtime 面试编排，把 `RealtimeEvent` 事件流转换成提问、回答、评分、追问、完成或错误。 |

## 为什么是这 5 个

### 1. `src/main.cpp`

它告诉你程序从哪里开始，以及依赖怎么被组装。新手看入口时，不要先陷进所有类的细节，先看清楚“谁创建谁，谁调用谁”。

### 2. `include/session/interview_manager.h`

它是面试领域规则的核心接口。读懂它，就知道这个项目所谓“面试流程”最少需要哪些动作：取当前题、记录回答、评分、追问、下一题。

### 3. `src/app/cli_interview_app.cpp`

它是最直观的可运行闭环。相比 WebSocket 和 Qt，CLI 更适合先理解数据流，因为输入输出都很直接。

### 4. `include/services/llm_client.h`

它体现了接口隔离：业务层只知道“我要生成问题、我要评分”，不知道背后是真模型、mock 还是 HTTP。

### 5. `src/session/dialog_orchestrator.cpp`

它是 realtime 主链路的入口。读它可以看到项目如何把实时事件变成业务动作，也能理解为什么 WebSocket 细节不能直接写进业务层。

## 建议阅读顺序

```text
src/main.cpp
  -> include/session/interview_manager.h
  -> src/app/cli_interview_app.cpp
  -> include/services/llm_client.h
  -> src/session/dialog_orchestrator.cpp
```

如果你只想先跑懂 CLI，就读前三个。如果你要接着理解 LLM 和 realtime，再读后两个。
