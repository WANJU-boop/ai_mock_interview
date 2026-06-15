# 2026-06-15 CLI 流程级测试闭环

## 本次目标

本次把终端版 mock 面试主流程从 `main` 中抽成可测试的应用层函数，并补上流程级测试。

这样做的原因是：上一阶段虽然已经形成“配置驱动的 mock 面试闭环”，但验证方式主要依赖模块测试和手动运行，缺少对“读取输入 -> 评分 -> 追问 -> 总结 -> JSON 报告”整条链路的自动回归保护。后续如果继续接 PDF、真实 LLM 或 UI，这里会成为最容易回归却最难定位的问题点。

## 修改了什么

- `include/app/cli_interview_app.h`：新增 `runCliInterview(...)` 公共入口，定义可复用、可测试的 CLI 流程函数。
- `src/app/cli_interview_app.cpp`：承接原来 `main` 里的状态切换、提问、追问、总结和报告输出逻辑。
- `src/main.cpp`：只保留日志初始化、配置加载、provider 创建和调用 `runCliInterview(...)`。
- `CMakeLists.txt`：新增 `app_lib`，把 CLI 应用层从可执行入口中独立出来。
- `test/app/test_cli_interview_app.cpp`：补两条流程级测试，分别覆盖“强回答直接结束”和“中间分数触发追问并更新报告”。
- `test/CMakeLists.txt`：把新的流程级测试文件接进测试目标。

## 核心数据流

1. `main` 读取配置、初始化日志并创建 `ILlmClient` 具体实现。
2. `main` 调用 `interview::app::runCliInterview(...)`，把输入流、输出流、配置和 LLM 抽象一起传入。
3. `runCliInterview(...)` 通过 `ILlmClient::generateQuestions()` 生成问题列表，再交给 `InterviewManager` 统一管理当前题目、评分和追问判断。
4. 每轮输入的回答先写入 `DialogSession`，再由 `InterviewManager` 调用注入的 `ILlmClient` 做评分。
5. 如果分数落在追问区间，流程函数继续读取追问回答，并用拼接后的文本重新评分。
6. 每题的最终结果都写入 `QuestionAnswerRecord`，总结输出和 JSON 报告都复用同一份结构化会话数据。

## C++ 知识点

- 应用层抽取（Application Layer Extraction）：把终端 I/O 编排从 `main` 中挪出，可以让 `main` 只承担启动职责，降低入口函数复杂度。
- 依赖注入（Dependency Injection）：`runCliInterview(...)` 依赖 `ILlmClient&`，测试可以直接传入 `MockLlmClient`，不需要网络和真实服务。
- 流抽象（Stream Abstraction）：使用 `std::istream` 和 `std::ostream` 代替直接读写 `std::cin/std::cout`，测试可通过字符串流完整模拟交互。
- 单一数据源（Single Source of Truth）：流程层不重复组装另一份报告结构，而是继续复用 `DialogSession` 与 `QuestionAnswerRecord`。

## 验证结果

- 编译：`cmake --build build -j` 通过。
- 单元测试：`ctest --test-dir build --output-on-failure` 通过，共 `59` 个测试。
- 新增验证：
  - `CliInterviewAppTest.CompletesInterviewWithoutFollowUpWhenAnswerIsStrong`
  - `CliInterviewAppTest.RequestsFollowUpAndStoresUpdatedScoreInReport`

## 注释自检

- 新增 public API `runCliInterview(...)` 已补中文注释，说明了抽层目的和测试价值。
- 新增 `.cpp` 中的状态切换、题目生成、输入异常、追问和报告输出逻辑均补了中文注释，重点解释“为什么这样分层”和“为什么这样记录数据”。
- 新增测试用例前都补了中文注释，说明验证目标和边界意义。

## 抽象是否接入主流程

这次新增的 `app` 抽象已经真正接入主流程。

`src/main.cpp` 现在直接调用 `runCliInterview(...)`，不再保留自己的问答循环，因此这个抽象不是悬空接口，而是当前 CLI 入口的真实执行路径。

## 下一步建议

下一步优先继续收口 CLI 闭环的错误路径测试，例如：

- 配置生成 `0` 道题时返回启动失败。
- 主回答后输入提前结束时进入错误状态。
- 未来接入真实 `HttpLlmClient` 前，先在 `app` 层补一个“服务异常如何反馈给用户”的最小策略。

这样做能让后续里程碑 3 到里程碑 4/5 的切换更稳，不会一引入真实服务就把当前可测闭环打散。
