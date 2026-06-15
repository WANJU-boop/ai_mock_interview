# 2026-06-13 配置驱动的 mock 面试闭环

## 本次目标

本次把已有的配置读取、LLM 抽象接口和文字版终端主流程真正接通，形成“配置驱动的 mock 面试闭环”。

这样做的原因是：仓库里已经有 `AppConfig`、`ILlmClient`、`MockLlmClient`、结构化问答记录和 JSON 报告，但入口 `main` 还在使用硬编码题目和本地评分逻辑。继续扩新模块之前，先把这一条主数据流打通，才能验证当前 milestone 的接口设计是否真的成立。

## 修改了什么

- `include/session/interview_manager.h`：让 `InterviewManager` 通过依赖注入持有 `ILlmClient`，评分结果直接复用服务层返回类型。
- `src/session/interview_manager.cpp`：删除本地重复的 mock 评分规则，改为调用注入的 `ILlmClient::scoreAnswer()`。
- `src/main.cpp`：新增配置加载、mock provider 创建、问题生成、评分接线，并在结束阶段输出 JSON 报告。
- `test/session/test_interview_manager.cpp`：补充“通过注入的 LLM 接口评分”的测试，以及追问阈值边界和 detail prompt 分支测试。
- `test/services/test_llm_client.cpp`：补充空岗位回退值和超出题库数量时追加编号的测试。
- `development_records/2026-06-13_config_driven_mock_interview_loop.md`：记录本次闭环的目标、数据流、知识点和验证结果。

## 核心数据流

1. 程序启动后读取 `config.example.json`，拿到候选人、岗位和题目数量配置。
2. 根据 `llm.provider` 创建 `MockLlmClient`。
3. `MockLlmClient` 先生成问题列表，再把问题交给 `InterviewManager` 管理当前题目索引和追问逻辑。
4. 用户输入主回答后，`InterviewManager` 通过注入的 `ILlmClient` 调用评分，而不是自己再实现一套本地规则。
5. 如果需要追问，主回答和追问回答会组合后重新评分。
6. 最终评分、结构化问答记录和 JSON 报告都写入同一个 `DialogSession`，保证 CLI 总结和后续导出读取的是同一份数据。

## C++ 知识点

- 依赖注入（Dependency Injection）：`InterviewManager` 不直接依赖 `MockLlmClient` 具体类型，而是依赖 `ILlmClient` 接口。这样后续替换成真实 HTTP 客户端时，管理器本身不需要改调用方式。
- 接口复用（Interface Reuse）：评分返回值直接复用 `services::LlmScoreResult`，避免 session 和 services 各维护一套内容相同但名字不同的数据结构。
- 引用成员（Reference Member）：`InterviewManager` 保存 `ILlmClient&`，表达“管理器不拥有客户端，只借用一个已经存在的服务对象”。
- 责任收口（Single Source of Truth）：把 mock 评分规则收口到 `MockLlmClient`，避免一个功能在两个模块里各写一套，后续修改时出现漂移。

## 验证结果

- 格式化：已运行 `clang-format -i include/session/interview_manager.h src/session/interview_manager.cpp src/main.cpp test/session/test_interview_manager.cpp test/services/test_llm_client.cpp`。
- 编译：`cmake --build build -j` 通过。
- 单元测试：`ctest --test-dir build --output-on-failure` 通过，共 `57` 个测试。
- 手动演示：已运行 `./build/AI_mock_interview`，确认能读取配置、生成问题、完成问答并输出 JSON 报告。

## 下一步建议

下一步优先把 CLI 主流程再抽一小层可测试的函数，补一个不依赖真实终端的流程级测试。这样后面继续接 PDF 或真实 LLM 时，能更快发现主流程回归。
