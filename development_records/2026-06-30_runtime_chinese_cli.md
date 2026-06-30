# 运行时中文化闭环

## 本次目标

把默认 CLI 运行体验从英文切换到中文，包括状态流、提问、回答提示、评分反馈、追问、总结、错误提示和默认 mock 题目。内部 C++ API 名、配置 key、JSON 报告字段名保持英文，避免破坏已有结构化数据和后续服务对接。

## 核心数据流

1. `main` 读取配置并创建 LLM client，初始化失败时输出中文错误前缀。
2. `prepareInterview` 调用 LLM client 生成题目；如果没有题目，返回中文失败原因。
3. `MockLlmClient` 生成中文题目，并给回答返回中文反馈。
4. `runCliInterview` 打印中文状态、题目、回答提示、评分、追问和总结。
5. `buildInterviewReportJson` 仍输出稳定英文 JSON key，但保存的题目、回答、反馈值来自中文运行流程。

## 关键 C++ 知识点

- 展示文案和业务判断要解耦：追问策略改成按分数段判断，不再比较某一句反馈文本。
- 中文回答不能只靠空格切词：mock 评分增加了多字节 UTF-8 字符统计，用来支持无空格中文长回答。
- JSON schema 不等于展示文案：`questions`、`score`、`feedback` 等字段名保留英文，字段值和终端输出中文化。
- 真实 HTTP LLM prompt 也要求中文输出，但继续约束模型返回同一套 JSON 结构。

## 验证结果

- 已运行 `clang-format -i` 格式化本次修改的 C++ 源码和测试文件。
- `cmake --build build -j` 通过；链接阶段仍有既有的 DWARF 调试信息解析 warning，不影响生成目标。
- `ctest --test-dir build --output-on-failure` 通过，92/92。
- 已用 `config.example.json` 手动运行 mock CLI，确认默认候选人、岗位、题目、状态、评分、总结和结束语均为中文。

## 下一步建议

- 如果后续引入 Qt UI，优先复用当前中文领域文案，不要在 UI 层重新硬编码一套英文/中文映射。
- 如果真实 LLM 返回更复杂的反馈类型，可以把 `LlmScoreResult` 扩展出独立的反馈类别字段，进一步减少对自然语言文本的依赖。
