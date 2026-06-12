# Codex 使用建议

## 常用提示词

开始一个阶段：

```text
Use $rebuild-cpp-interview to implement milestone 1. Explain the learning goal before editing, keep the patch small, and run the available verification.
```

讲解一段代码：

```text
Use $cpp-learning-mentor to explain this file for a beginner. Focus on ownership, data flow, and how to test it.
```

让 Codex 自检：

```text
Review the current diff as a code reviewer. Prioritize bugs, test gaps, secrets, and maintainability issues.
```

准备提交：

```text
Summarize git status, git diff, and test status. Then suggest a commit message for this milestone.
```

## 子代理使用

当任务复杂时，主线程继续编码，子代理做独立复核。

适合子代理的任务：

- 检查 CMake、vcpkg、入口文件和依赖边界。
- 读代码并指出架构风险。
- 为某个模块设计测试清单。
- 检查 LLM/API 日志和密钥泄露风险。
- 检查 PR 描述和提交范围。
- 复核并发、回调、Qt UI 线程风险。

不适合子代理的任务：

- 多个代理同时改同一个文件。
- 让代理处理需要真实密钥的操作。
- 让代理做没有边界的大规模重构。

## 推荐节奏

每个 milestone 都按这个循环：

1. 让 Codex 读当前文件和路线。
2. 让 Codex 说明本次目标。
3. 让 Codex 实现最小改动。
4. 运行验证。
5. 让 Codex 用新手视角复盘。
6. 提交。
