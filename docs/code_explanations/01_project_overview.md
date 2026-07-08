# 项目整体概览

## 项目是做什么的

这是一个 C++ AI 模拟面试系统的学习型复刻项目。当前目标不是一次性做完整桌面产品，而是先用可运行、可测试的小闭环理解整体架构：

```text
读取配置
  -> 准备候选人和岗位信息
  -> 生成面试问题
  -> 收集候选人回答
  -> 评分、反馈、必要时追问
  -> 生成结构化报告
  -> 后续替换成真实 LLM、PDF、Realtime WebSocket、音频和 Qt UI
```

当前仓库已经具备 CLI 面试流程、mock LLM、HTTP LLM 边界、PDF parser 边界、realtime mock 流程、火山 Realtime 文本模式客户端和供应商协议适配层。

## 技术栈

- C++17：项目主语言。
- CMake：组织库、可执行程序和测试 target。
- vcpkg：管理第三方依赖。
- GoogleTest：单元测试和流程级测试。
- spdlog：日志。
- nlohmann-json：配置、LLM 请求/响应、报告和供应商 payload。
- Boost.Asio / Boost.Beast / OpenSSL：同步 HTTP、HTTPS、WSS/WebSocket 传输。
- PoDoFo：真实 PDF 文本解析。

## 目录怎么组织

```text
include/
  common/      配置、日志、项目内部 realtime 事件协议
  services/    LLM、HTTP、PDF、Realtime、火山协议等外部能力边界
  session/     面试领域逻辑、状态、问答记录、评分、报告、realtime 编排
  app/         CLI 和 demo 级应用编排

src/
  common/      common 实现
  services/    services 实现
  session/     session 实现
  app/         app 实现

test/
  common/      配置、协议等底层测试
  services/    LLM、HTTP、PDF、Realtime、火山协议测试
  session/     面试流程和领域逻辑测试
  app/         CLI 和 realtime demo 流程测试

docs/          学习路线、工作流、代码解释文档
development_records/
               每个阶段闭环的中文记录
```

## 入口在哪里

主 CLI 入口是 `src/main.cpp`，构建目标是 `AI_mock_interview`。

它只做依赖组装：

```text
初始化日志
  -> 找配置文件
  -> loadConfigFromFile
  -> createLlmClient
  -> PodofoPdfParser
  -> prepareInterview
  -> runCliInterview
```

Realtime mock 入口是 `src/main_realtime_demo.cpp`，构建目标是 `AI_mock_interview_realtime_demo`。它复用配置和取题链路，只把候选人输入换成固定 realtime 事件脚本。

真实火山文本 demo 入口是 `src/main_volc_realtime_text_demo.cpp`，构建目标是 `AI_mock_interview_volc_text_demo`。它从环境变量读取火山凭据，执行真实 WSS 文本模式 smoke test，不属于默认单元测试。
