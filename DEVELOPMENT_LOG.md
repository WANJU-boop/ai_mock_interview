# DEVELOPMENT_LOG.md

## 文件作用

本文档用于记录 C++ AI 模拟面试项目（AI Mock Interview）的项目进度、设计决策、当前任务和后续顺序，防止不同 Codex thread 之间上下文丢失。

每次完成任务后，都应更新本文件，记录修改文件、实现内容、测试方式和下一步最小任务。

本文档是当前下一步任务的唯一来源。项目总览文档（PROJECT_OVERVIEW.md）只记录长期架构和实现顺序建议，不记录容易过期的当前任务。

## 维护规则

- 每次完成开发任务后，必须更新本文件。
- 如果任务改变了当前进度、已完成事项或下一步最小任务，必须同步更新对应章节。
- 如果本次任务不影响开发进度，例如只修改 Codex 配置、Git 工作流或说明文字，可以不新增任务记录，但最终回复必须说明原因。
- 新的 Codex thread 或子代理（Subagent）判断下一步任务时，以本文档为准。
- 不要在多个 Markdown 文件中重复维护“当前下一步任务”，避免过期信息互相冲突。

## 当前项目状态

- 项目类型：C++ AI 模拟面试项目（AI Mock Interview）。
- 长期目标：AI 面试系统（AI Interview System）。
- 当前目标：C++ 文字版最小可行产品（text-based C++ mock MVP）。
- 当前优先方向：先实现状态枚举（InterviewState），再逐步实现对话会话（DialogSession）和面试流程管理器（InterviewManager）。
- 当前服务层策略：实时客户端（RealtimeClient）、音频管理器（AudioManager）、大模型客户端（LLMClient）、简历解析器（PDFParser）暂时只保留在设计文档中，MVP 阶段不实现真实服务。

## 已完成或已讨论内容

- CMake 项目结构（CMake project structure）：项目已经具备基础构建配置。
- 日志系统（Logger / spdlog）：已有日志系统（Logger），底层使用日志库（spdlog）。
- 构建环境（build environment）：项目使用 CMake 和依赖清单（vcpkg manifest）管理依赖。
- 单元测试框架（GoogleTest）：已有日志相关测试。
- 四层架构（four-layer architecture）：应用层（Application Layer）、会话层（Session Layer）、服务层（Service Layer）、基础层（Foundation Layer）。
- 状态机流程（state machine flow）：已整理连接、面试官说话、空闲等待、候选人回答、面试官思考、结束处理、完成等状态。
- 面试时序流程（interview sequence flow）：已整理初始化、开场白、问答循环、候选人回答、评分、追问或下一题、总结、结束清理等阶段。

## 当前下一步任务

添加状态枚举（InterviewState）。

建议最小范围：

- 新增或选择一个合适的头文件保存状态枚举（InterviewState）。
- 只定义状态，不实现对话会话（DialogSession）。
- 状态名建议与状态机流程保持一致，例如 `kConnecting`、`kInterviewerSpeaking`、`kIdle`、`kCandidateSpeaking`、`kInterviewerThinking`、`kSessionEnding`、`kCompleted`。
- 可考虑补充错误状态（kError），但应在写代码前向用户解释它是工程扩展状态。

## 后续计划顺序

1. 检查当前项目结构。
2. 检查日志系统（Logger）当前用法。
3. 添加状态枚举（InterviewState）。
4. 添加对话会话（DialogSession）状态管理。
5. 添加对话会话（DialogSession）对话历史。
6. 添加面试流程管理器（InterviewManager）固定问题列表。
7. 添加用户回答保存。
8. 添加模拟评分（mock scoring）。
9. 添加命令行演示（CLI demo）。
10. 添加简单测试。

## 每次任务完成后的记录模板

```markdown
## YYYY-MM-DD

### 任务名称

- 修改文件：
- 实现内容：
- 测试方式：
- 测试结果：
- 是否能编译：
- 下一步最小任务：
```

## 2026-06-06

### 整理文字版项目设计文档

- 修改文件：`AGENTS.md`、`PROJECT_OVERVIEW.md`、`DEVELOPMENT_LOG.md`。
- 实现内容：根据项目架构图、层级划分图、状态机流程图和面试时序图，整理 Codex 协作规则、项目总览、状态机设计、时序流程和后续任务顺序。
- 测试方式：仅文档修改，不运行编译或单元测试。
- 测试结果：不适用。
- 是否能编译：未验证，因为本次未修改 C++ 源文件或构建配置。
- 下一步最小任务：添加状态枚举（InterviewState）。

## 需要用户确认

- 时序图中的面试会话（InterviewSession）是否等同于架构图中的面试流程管理器（InterviewManager）。
- 追问分支中的分数阈值是否固定采用 70-89 分需要追问、90 分以上不需要追问。
- 服务器语音服务（ASR/TTS Server）后续是否作为独立服务层模块。
