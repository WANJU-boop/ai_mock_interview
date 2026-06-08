# PROJECT_OVERVIEW.md

## 项目目标

本项目是 C++ AI 模拟面试项目（AI Mock Interview）。

- 长期目标：构建 AI 面试系统（AI Interview System），支持面试流程、候选人回答、智能评分和总结反馈。
- 当前目标：先完成 C++ 文字版最小可行产品（text-based C++ mock MVP），用命令行或简单文本交互模拟面试流程，不立即实现真实语音、实时网络或大模型调用。

## 四层架构

项目设计采用四层架构（four-layer architecture），从上到下依次为应用层、会话层、服务层和基础层。

### 应用层（Application Layer）

应用层负责用户交互、界面展示和参数配置。

- 用户界面（UI）：长期可以承载面试界面。
- 命令行演示（CLI demo）：当前 MVP 更适合先使用命令行实现文字版流程。
- 参数配置（parameter configuration）：负责控制面试题数、模式、输入输出等用户可见配置。

### 会话层（Session Layer）

会话层负责业务流程、会话管理和状态机驱动，是当前 MVP 的核心。

- 对话会话（DialogSession）：负责保存一次面试中的当前状态、问题、回答和上下文。
- 面试流程管理器（InterviewManager）：负责推进面试流程，例如开场、出题、接收回答、评分、追问、总结和结束。
- 状态机驱动（state machine driven flow）：通过状态枚举（InterviewState）描述面试处于哪个阶段。

图片中的时序图出现面试会话（InterviewSession）作为“面试逻辑”，需要用户确认它是否等同于面试流程管理器（InterviewManager），还是后续需要作为单独类存在。

### 服务层（Service Layer）

服务层负责具体技术能力的实现。当前 MVP 暂时不实现真实服务，只保留设计边界。

- 实时客户端（RealtimeClient）：设计上用于 WebSocket 连接和实时通信。
- 音频管理器（AudioManager）：设计上用于麦克风录音、音频输入输出和播放。
- 大模型客户端（LLMClient）：设计上用于 AI 评分、问题生成、追问生成和总结生成。
- 简历解析器（PDFParser）：设计上用于简历解析。
- 服务器语音服务（ASR/TTS Server）：时序图中出现的服务端语音识别和语音合成能力，当前 MVP 不实现真实调用。

### 基础层（Foundation Layer）

基础层提供项目基础能力和通用支撑。

- 配置模块（Config）：负责基础配置。
- 协议/数据结构（Protocol）：负责协议编解码和数据结构定义。
- 日志系统（Logger）：负责统一日志输出，后续模块必须复用。
- 工具函数（Utils）：负责通用辅助函数。

## 状态机设计

状态枚举（InterviewState）用于描述一次面试会话的当前阶段。根据状态机流程图，核心状态如下：

- 连接中（Connecting / kConnecting）：系统建立连接或准备会话资源。
- 面试官说话中（InterviewerSpeaking / kInterviewerSpeaking）：面试官播放开场白、问题、追问或总结。
- 空闲等待中（Idle / kIdle）：系统等待候选人开始回答，或等待进入下一步。
- 候选人回答中（CandidateSpeaking / kCandidateSpeaking）：候选人正在回答问题。
- 面试官思考中（InterviewerThinking / kInterviewerThinking）：系统记录回答、评分或决定是否追问。
- 会话结束处理中（SessionEnding / kSessionEnding）：系统生成总结、保存报告并关闭连接。
- 已完成（Completed / kCompleted）：面试流程完成。
- 错误状态（Error / kError）：图片中未明确出现，但工程上建议作为扩展状态，用于表示连接失败、评分失败、输入异常等错误场景。

## 状态流转

主流程可以整理为：

```text
Connecting
→ InterviewerSpeaking
→ Idle
→ CandidateSpeaking
→ InterviewerThinking
→ InterviewerSpeaking
→ Idle
→ ...
→ SessionEnding
→ Completed
```

更具体的文字说明：

- 连接中（Connecting / kConnecting）完成后，进入面试官说话中（InterviewerSpeaking / kInterviewerSpeaking）播放开场白。
- 开场白结束后，进入空闲等待中（Idle / kIdle）。
- 候选人开始回答后，进入候选人回答中（CandidateSpeaking / kCandidateSpeaking）。
- 候选人回答结束后，进入面试官思考中（InterviewerThinking / kInterviewerThinking）。
- 如果需要下一题或追问，回到面试官说话中（InterviewerSpeaking / kInterviewerSpeaking），再进入空闲等待中（Idle / kIdle）。
- 如果进入总结流程，则进入会话结束处理中（SessionEnding / kSessionEnding）。
- 结束清理完成后，进入已完成（Completed / kCompleted）。

## 时序流程

根据面试时序图，文字版流程可整理为以下阶段。

### 初始化阶段

- 对话会话（DialogSession）初始化面试逻辑。
- 面试逻辑生成问题或读取简历相关问题。
- 面试逻辑调用大模型（LLM）生成或准备问题。
- 对话会话（DialogSession）建立 WebSocket 连接。
- 服务器语音服务（ASR/TTS Server）返回连接成功。
- 对话会话（DialogSession）启动音频设备。

当前 MVP 可简化为：初始化状态枚举（InterviewState）、准备固定问题列表，并输出日志系统（Logger）记录。

### 开场白阶段

- 系统发送开场白文本。
- 服务器语音服务（ASR/TTS Server）返回 TTS 音频流。
- 面试官播放“你好，欢迎面试”等开场内容。

当前 MVP 可简化为：在命令行中打印开场白文本。

### 问答循环阶段

- 每道题进入循环。
- 对话会话（DialogSession）从面试逻辑获取问题。
- 如果问题来自 AI，可能需要请求大模型（LLM）。
- 系统发送问题文本。
- 服务器语音服务（ASR/TTS Server）返回 TTS 音频。
- 面试官播放问题。

当前 MVP 可简化为：使用固定问题列表逐题输出。

### 候选人回答阶段

- 候选人通过麦克风回答。
- 对话会话（DialogSession）发送音频流。
- 服务器语音服务（ASR/TTS Server）返回语音识别结果。
- 对话会话（DialogSession）记录回答文本。

当前 MVP 可简化为：从命令行读取候选人输入，并保存为回答文本。

### 评分阶段

- 对话会话（DialogSession）把回答交给面试逻辑。
- 面试逻辑调用大模型（LLM）评分。
- 大模型返回类似 `{score:75, need_followup:true}` 的评分结果。

当前 MVP 可简化为：使用模拟评分（mock scoring），不调用真实大模型。

### 追问或下一题分支

- 如果需要追问，例如分数处于 70-89 区间，则返回追问问题。
- 系统发送追问文本并播放追问。
- 候选人回答追问后，系统再次得到语音识别结果。
- 如果不需要追问，例如分数达到 90 分以上，则进入下一题。

图片中能看到追问分支和不需要追问分支，但具体分数阈值是否固定为 70-89 和 90+，需要用户确认。

### 总结阶段

- 面试逻辑生成总结。
- 大模型（LLM）生成综合评价和总结建议。
- 系统发送总结文本。
- 服务器语音服务（ASR/TTS Server）返回 TTS 音频。
- 面试官播放总结。
- 对话会话（DialogSession）保存报告。

当前 MVP 可简化为：根据已保存回答输出文字版总结。

### 结束清理阶段

- 关闭连接。
- 清理资源。
- 状态进入已完成（Completed / kCompleted）。

## 实现顺序建议

建议先完成状态枚举（InterviewState）相关基础能力，再逐步实现对话会话（DialogSession）和面试流程管理器（InterviewManager）。

原因：

- 对话会话（DialogSession）需要保存当前状态。
- 面试流程管理器（InterviewManager）需要根据状态推进流程。
- 状态枚举（InterviewState）范围小、依赖少、适合作为早期基础模块。
- 先定义状态，可以避免后续在 DialogSession 中使用散落的字符串或布尔变量表达流程阶段。

具体的当前下一步任务以开发记录文档（DEVELOPMENT_LOG.md）为准，本文档只保留长期架构和实现顺序建议。

## 需要用户确认

- 时序图中的面试会话（InterviewSession）是否等同于架构图中的面试流程管理器（InterviewManager）。
- 追问分支中的分数阈值是否固定采用 70-89 分需要追问、90 分以上不需要追问。
- 服务层中的服务器语音服务（ASR/TTS Server）后续是否作为独立模块，还是归入实时客户端（RealtimeClient）或音频管理器（AudioManager）。
