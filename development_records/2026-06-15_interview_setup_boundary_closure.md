# 2026-06-15 InterviewSetup 启动边界收口

## 本次目标

本次把“根据配置向 LLM 取题并准备可运行面试上下文”的启动逻辑，从 CLI 编排层收口到 `session` 层。

这样做的原因是：上一阶段虽然已经形成了配置驱动的 mock CLI 面试闭环，但 `app` 层同时负责了服务取题、失败判断、状态编排、总结输出，边界开始变混。后续如果继续接 `HttpLlmClient`、PDF 或 UI，这里会让依赖方向越来越难收拾。

## 修改了什么

- `include/session/interview_setup.h`、`src/session/interview_setup.cpp`
  - 新增 `PreparedInterview` 和 `prepareInterview(...)`。
  - 由 `session` 层统一负责把 `InterviewConfig` 和 `ILlmClient` 组合成可直接运行的面试上下文。
- `include/app/cli_interview_app.h`、`src/app/cli_interview_app.cpp`
  - `runCliInterview(...)` 不再直接接收配置和 LLM 接口。
  - CLI 层现在只消费 `PreparedInterview`，专注于输入输出编排、状态流转、总结和报告。
- `src/main.cpp`
  - `main` 保留配置加载、provider 创建、调用 `prepareInterview(...)` 和启动 CLI 的职责。
- `test/session/test_interview_setup.cpp`
  - 新增启动准备层测试，覆盖成功准备、参数透传、无题失败。
- `test/app/test_cli_interview_app.cpp`
  - 新增 CLI 边界测试，覆盖准备失败、主回答 EOF、追问 EOF、多题顺序。
- `CMakeLists.txt`、`test/CMakeLists.txt`
  - 把新的 `session` 源文件和测试文件纳入构建。

## 核心数据流

1. `main` 读取 `AppConfig`，并根据 provider 创建 `ILlmClient` 具体实现。
2. `main` 调用 `session::prepareInterview(config.interview, *llm_client)`。
3. `prepareInterview(...)` 负责：
   - 把 `candidate_name`、`target_role`、`question_count` 透传给 `generateQuestions(...)`。
   - 在取题成功时构造 `PreparedInterview`，内部持有 `InterviewManager`。
   - 在取题失败时返回带错误信息的未就绪上下文。
4. `app::runCliInterview(...)` 只判断 `PreparedInterview` 是否 ready：
   - ready：驱动问答、评分、追问、总结、报告。
   - not ready：输出 `session` 层准备好的错误信息并退出。

## 关键 C++ 知识点

- 依赖方向（Dependency Direction）
  - 让启动准备逻辑落在 `session`，比让 `app` 直接碰服务取题更符合 `app/ui -> interview -> services -> common` 的目标方向。
- 组合根（Composition Root）
  - `main` 仍然负责创建具体 provider，但“创建好之后如何准备可运行会话”已经交给领域侧抽象处理。
- 上下文对象（Context Object）
  - `PreparedInterview` 把候选人信息、错误信息和 `InterviewManager` 组合在一起，减少入口层散落的参数和判断。
- 可测试接线（Testable Wiring）
  - 新增 `InterviewSetupTest` 后，启动参数透传和无题失败不再只能靠 CLI 端到端测试间接覆盖。

## 验证结果

- 配置：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 通过。
- 编译：`cmake --build build -j` 通过。
- 测试：`ctest --test-dir build --output-on-failure` 通过，共 `66` 个测试。

## 注释自检

- 新增 public API `PreparedInterview` 和 `prepareInterview(...)` 已补中文注释，说明抽象目的和职责边界。
- 新增测试用例前都补了中文注释，说明验证的是哪条启动/错误路径以及为什么重要。
- `.cpp` 中保留了关键中文注释，重点解释“为什么把启动取题逻辑收口到 interview 层”。

## 抽象是否接入主流程

这次新增的 `InterviewSetup` 抽象已经真正接入主流程。

`src/main.cpp` 现在先调用 `prepareInterview(...)`，再调用 `runCliInterview(...)`。CLI 启动失败分支也已经改成消费 `PreparedInterview` 里的错误信息，因此它不是悬空接口。

## 下一步建议

下一步优先做两件事中的一件，但不要同时展开：

1. 继续收口构建层噪音
   - 清理当前未使用的 `OpenSSL` / `Boost` 依赖。
   - 复查测试 target 的冗余 link 关系。
2. 进入真实 LLM 客户端骨架
   - 新增 `HttpLlmClient` 和 provider factory。
   - 保持默认单元测试仍只依赖 mock，把真实 HTTP 检查放到手动集成验证。
