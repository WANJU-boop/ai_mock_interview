# 阶段 4：报告持久化与 LLM 可靠性边界

## 本次目标

将原先只打印到终端的面试报告接入可配置的本地 JSON 文件导出，并收紧 LLM 输入与结构化输出
校验。这样主流程的终点不再是临时文本，而是一份可复查、不会静默覆盖的报告文件。

## 核心数据流

```text
config.report
  -> main / realtime demo
  -> app 导出决策
  -> createInterviewReportPath
  -> saveInterviewReportJson
       -> 同目录 .tmp
       -> rename
       -> reports/interview-report-<time>-<sequence>.json

PDF 简历文本
  -> llm.max_prompt_context_chars 截断
  -> HTTP LLM JSON 请求
  -> 非空白题目、数量一致、0..100 分数、非空白反馈校验
  -> InterviewManager / 报告
```

## 关键设计

- 报告文件名不使用候选人姓名、岗位或简历名；生成文件目录已被 `.gitignore` 忽略。
- 导出先写临时文件再 `rename`，目标或临时文件已存在时明确失败，避免覆盖候选人记录。
- 报告正文不会进入日志；CLI/Realtime 只显示最终路径或错误摘要。
- 简历上下文只截取配置指定的字符数；被截断部分不会进入 HTTP 请求或日志。
- 题目数量必须和请求计划一致，题目/反馈不能是空白字符串，防止半合法模型输出污染会话状态。

## 验证

- 报告测试覆盖原子写入、JSON 内容、临时文件清理、拒绝覆盖和隐私化文件名。
- CLI 流程测试覆盖“完成面试 -> 写出报告 -> 输出路径”的真实应用层数据流。
- HTTP LLM fake 测试覆盖简历截断、空白题目、数量不符和空白 feedback。
- 配置测试覆盖 report 映射、默认值与 `max_prompt_context_chars` 正数约束。

## 下一步建议

阶段 0–4 已形成 CLI/音频/报告闭环。下一阶段应进入 Qt：把整个同步 realtime 面试放到
可 join 的 worker，使用 queued signal 回主线程显示状态和 partial transcript，再补配置窗口、
取消按钮和报告打开入口。
