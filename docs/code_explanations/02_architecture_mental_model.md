# 架构心智模型

## 一句话记住这个项目

这个项目是一个“面试业务核心 + 可替换外部服务”的 C++ 学习骨架：`session` 负责面试怎么进行，`services` 负责外部能力怎么接入，`common` 提供稳定基础结构，`app/main` 只负责把它们组装起来跑。

## 核心模块

### common

`common` 是底座，不依赖业务和外部服务。

它负责：

- 配置结构和配置文件读取。
- 日志初始化和统一日志接口。
- 项目内部 realtime 事件协议：`RealtimeEvent`、`RealtimeEventType`、frame 编解码。

心智模型：`common` 提供大家都能使用的稳定语言。

### services

`services` 是外部能力适配层。

它负责：

- `ILlmClient`：题目生成和回答评分。
- `IHttpTransport` / `HttpLlmClient` / `BeastHttpTransport`：HTTP LLM 调用。
- `IPdfParser` / `PodofoPdfParser`：简历 PDF 解析。
- `IRealtimeClient` / `MockRealtimeClient`：realtime 服务抽象和确定性 mock。
- `VolcRealtimeClient` / `BeastVolcRealtimeTransport` / `VolcRealtimeClientAdapter`：火山 Realtime WSS、二进制协议和项目事件适配。

心智模型：`services` 把不稳定、难测试、依赖外部环境的东西关在边界里。

### session

`session` 是面试领域核心。

它负责：

- `InterviewManager`：管理当前题目、记录回答、调用 LLM 评分、判断是否追问。
- `DialogSession`：保存一次面试中的状态、回答、评分和结构化问答记录。
- `InterviewReport`：把会话数据导出成 JSON 报告。
- `InterviewSetup`：启动阶段根据配置、简历和 LLM 准备可运行面试。
- `DialogOrchestrator`：把 realtime 事件流接入面试主流程。

心智模型：`session` 不关心 LLM、PDF、WebSocket 的真实实现，只关心面试规则。

### app

`app` 是应用层编排。

它负责：

- `runCliInterview`：终端文字面试流程。
- `runRealtimeDemoInterview`：用 mock realtime 事件跑一次实时面试 demo。

心智模型：`app` 负责输入输出和流程入口，不沉淀核心业务规则。

## 依赖关系

推荐记成一条单向链：

```text
main/app
  -> session
  -> services
  -> common
```

更准确地说：

- `main` 创建配置、LLM client、PDF parser，再调用 app/session。
- `app` 调用 `session` 跑 CLI 或 realtime demo。
- `session` 通过接口依赖 `services`，例如 `ILlmClient`、`IPdfParser`、`IRealtimeClient`。
- `services` 使用 `common` 的配置或内部事件类型。
- `common` 不反向依赖任何业务模块。

这条依赖方向很重要：越靠内层越稳定，越靠外层越接近真实环境。

## 主数据流

### CLI 面试流

```text
main.cpp
  -> loadConfigFromFile
  -> createLlmClient
  -> prepareInterview
  -> runCliInterview
  -> InterviewManager::getCurrentQuestion
  -> 用户输入回答
  -> InterviewManager::scoreCandidateAnswer
  -> InterviewManager::decideFollowUp
  -> DialogSession 保存记录
  -> buildInterviewReportJson
```

CLI 流程是最容易理解的主链路：输入来自 `std::cin`，输出到 `std::cout`，适合先学习领域逻辑。

### Realtime mock 流

```text
main_realtime_demo.cpp
  -> prepareInterview
  -> buildDefaultRealtimeDemoScript
  -> MockRealtimeClient
  -> DialogOrchestrator::run
  -> kConnected 后发送欢迎语和题目
  -> kTranscriptFinal 后评分和追问
  -> DialogSession 保存最终记录
  -> buildInterviewReportJson
```

Realtime mock 流验证“实时事件驱动面试”这件事，但不碰真实网络、麦克风和服务端账号。

### 真实火山文本模式流

```text
main_volc_realtime_text_demo.cpp
  -> loadConfigFromFile 读取唯一 JSON 配置
  -> resolveVolcRealtimeRuntimeConfig
  -> 按配置指定的环境变量读取 App ID / Access Key，并生成运行时 connection/session ID
  -> BeastVolcRealtimeTransport
  -> VolcRealtimeClient
  -> WSS handshake
  -> StartConnection / ConnectionStarted
  -> StartSession(text) / SessionStarted
  -> ChatTextQuery
  -> ChatResponse ... ChatEnded
  -> FinishSession
  -> FinishConnection
  -> close
```

当前真实火山链路是手动集成检查，不进入默认单元测试。

## 关键设计决策

### 先 mock，再真实服务

真实 LLM、PDF、WebSocket、音频都会引入网络、文件、权限、账号、TLS 和不确定响应。项目先写接口和 mock，保证默认测试稳定，再逐步替换真实实现。

### 业务层不直接依赖 WebSocket

`DialogOrchestrator` 只依赖 `IRealtimeClient` 和 `RealtimeEvent`，不认识 Boost.Beast、火山 event id 或二进制 frame。这样未来换供应商、换传输库、接 Qt UI 时，面试业务不用重写。

### 供应商协议和项目事件分开

火山自己的 `VolcRealtimeFrame` 留在 `services` 层；项目内部统一使用 `common::RealtimeEvent`。`VolcRealtimeClientAdapter` 是翻译层。

这能避免火山协议细节扩散到 `session`、`app` 和未来 UI。

### 错误尽早暴露，资源明确关闭

配置缺字段、未知 event id、payload 长度越界、空文本 query 都尽早失败。realtime 错误、提前关闭、事件流耗尽都会进入 `InterviewState::kError` 并调用 `close()`。

### 分数决定流程，文案只负责展示

追问策略只看分数段，不依赖反馈文案。这样 mock LLM、真实 LLM 或中文反馈文案变化时，不会意外改变流程控制。

### 默认测试不能依赖外部环境

单元测试使用 fake transport 和 mock client。真实网络、真实 PDF 环境、真实服务账号只放到手动集成 demo。

## 快速回忆版

如果以后只想用 30 秒回忆：

```text
common 定义稳定基础语言。
services 隔离 LLM/PDF/WebSocket 等外部能力。
session 保存面试规则、状态、评分、追问和报告。
app/main 只做依赖组装和输入输出。

主链路是：
配置 -> 准备面试 -> 提问 -> 回答 -> 评分 -> 追问/下一题 -> 报告。

realtime 链路是：
WebSocket/供应商 frame -> adapter -> RealtimeEvent -> DialogOrchestrator -> InterviewManager -> DialogSession -> Report。
```
