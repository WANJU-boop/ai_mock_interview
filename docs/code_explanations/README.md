# 代码解释文档索引

这个目录是项目代码导读的统一入口，用来快速回忆“这个项目是什么、怎么分层、一次请求如何流动、每个阶段学了什么”。

## 推荐阅读顺序

1. [项目整体概览](01_project_overview.md)
2. [架构心智模型](02_architecture_mental_model.md)
3. [Realtime 与 WebSocket 流程](03_realtime_websocket_flow.md)

## 现有资料整理

### 项目路线

- [复刻学习路线](../rebuild_learning_path.md)：按 15 天学习节奏拆分 CMake、配置、领域逻辑、LLM、PDF、realtime、Qt。
- [源项目结构速览](../source_project_map.md)：记录参考项目的模块边界和新手风险点。
- [Codex 使用说明](../codex_usage.md)：说明本项目中如何配合 Codex 推进学习。
- [GitHub 工作流](../github_workflow.md)：记录分支、提交和 PR 的协作方式。

### 阶段记录

- [结构化问答记录](../../development_records/2026-06-10_structured_question_answer_record.md)
- [配置驱动的 mock 面试闭环](../../development_records/2026-06-13_config_driven_mock_interview_loop.md)
- [CLI 流程级测试闭环](../../development_records/2026-06-15_cli_flow_level_test_closure.md)
- [默认配置路径修复](../../development_records/2026-06-15_default_config_path_fix.md)
- [HTTP LLM 客户端骨架](../../development_records/2026-06-15_http_llm_client_skeleton.md)
- [HTTP transport 集成闭环](../../development_records/2026-06-15_http_transport_integration_closure.md)
- [面试准备边界闭环](../../development_records/2026-06-15_interview_setup_boundary_closure.md)
- [LLM provider factory 闭环](../../development_records/2026-06-15_llm_provider_factory_closure.md)
- [PDF parser 边界闭环](../../development_records/2026-06-30_pdf_parser_boundary_closure.md)
- [PoDoFo PDF parser 闭环](../../development_records/2026-06-30_podofo_pdf_parser_closure.md)
- [运行时中文 CLI](../../development_records/2026-06-30_runtime_chinese_cli.md)
- [Realtime mock 流程闭环](../../development_records/2026-07-03_realtime_mock_flow_closure.md)
- [火山 Realtime 协议边界](../../development_records/2026-07-06_volc_realtime_protocol_boundary.md)
- [火山 Realtime 文本模式客户端](../../development_records/2026-07-06_volc_realtime_text_client.md)
- [火山 Realtime 适配到项目接口](../../development_records/2026-07-06_volc_realtime_adapter.md)

## 维护约定

- 新增一块重要代码导读时，优先放到本目录。
- 新增一个阶段性实现记录时，仍放到 `development_records/`，再从本索引链接过去。
- 不把真实 API key、access key、候选人完整回答或简历全文写进解释文档。
