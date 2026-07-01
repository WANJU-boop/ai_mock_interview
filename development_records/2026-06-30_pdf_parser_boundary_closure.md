# PDF 简历解析边界闭环

## 本次目标

补上 milestone 4 的第一个外部服务边界：PDF 简历解析接口。当前阶段不接真实 PDF 库，而是先建立 `IPdfParser` 和 `MockPdfParser`，让配置里的可选 `resume_path` 能进入“准备面试 -> 解析简历 -> 生成题目”的主流程。

这样做的原因是：真实 PDF 解析会引入第三方库、文件格式差异和本机环境问题。如果一开始直接接真实库，学习重点会从“架构边界和数据流”偏到依赖排障。先用 mock 跑通接口，可以让后续替换真实解析器时只改 services 层。

## 核心数据流

1. `loadConfigFromFile` 从 `interview.resume_path` 读取可选简历路径；为空时保持无简历流程。
2. `main` 创建 `MockPdfParser`，并和 `ILlmClient` 一起注入 `prepareInterview`。
3. `prepareInterview` 在启动阶段调用 `IPdfParser::parseResume`，把解析出的摘要作为 `resume_context`。
4. `QuestionGenerationRequest` 增加 `resume_context`，题目生成服务可以据此定制题目。
5. `MockLlmClient` 在有简历上下文时优先生成一题“结合简历项目经历”的题目，但不把简历原文拼进题目。
6. `HttpLlmClient` 把简历上下文放入题目生成 prompt，并明确要求模型不要原文复述。

## 关键 C++ 知识点

- 接口隔离：`IPdfParser` 让 interview 层只依赖“解析能力”，不依赖具体 PDF 库。
- 依赖注入：`prepareInterview` 显式接收 `IPdfParser&`，避免配置里有简历路径却忘记接入解析器。
- Mock 边界：`MockPdfParser` 不读取真实文件，只返回确定性上下文，保证单元测试不依赖 PDF fixture。
- 错误收口：简历解析异常和空文本都在启动阶段转成 `PreparedInterview` 失败结果，CLI 不需要知道 PDF 解析细节。
- 隐私边界：简历上下文只进入题目生成请求，不写日志、不写报告、不在 mock 题目中复述原文。

## 验证结果

- 已运行 `clang-format -i` 格式化本次修改和新增的 C++ 源码、头文件、测试文件。
- `cmake --build build -j` 通过；第一次在沙箱内因 vcpkg 根目录锁文件权限失败，提升权限后构建成功。链接阶段仍有既有的 DWARF 调试信息解析 warning，不影响目标生成。
- `ctest --test-dir build --output-on-failure` 通过，102/102。

## 下一步建议

- 继续 milestone 4 时，优先补 realtime 协议/事件的纯数据边界，用固定事件 fixture 测试解析和状态流。
- 第二轮再接真实 PDF 库；接入时保持 `IPdfParser` 不变，只新增真实实现和手动集成检查。
