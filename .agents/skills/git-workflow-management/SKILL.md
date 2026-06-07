---
name: git-workflow-management
description: C++ AI Mock Interview Git 工作流管理；当用户要求查看 diff、生成 commit message、暂存或提交变更时使用，禁止自动 push，commit 前必须展示 diff 总结和测试状态。
---

# Git Workflow Management

## 使用场景

当用户在 C++ AI 模拟面试项目（AI Mock Interview）中要求管理 Git 工作流（Git workflow）时，必须使用本技能。

典型任务包括：

- 查看当前差异（diff）。
- 总结工作区状态（working tree status）。
- 生成提交信息（commit message）建议。
- 暂存文件（git stage）。
- 执行提交（git commit）。

## 核心原则

- 不要自动推送（push）。
- 不要修改任何源码文件。
- 不要修改 C++ 源文件（.cpp）。
- 不要修改 C++ 头文件（.h / .hpp）。
- 不要修改构建配置文件（CMakeLists.txt）。
- 不要添加新功能。
- 不要重构代码。
- 不要把无关文件混进同一次提交。
- 如果工作区存在用户已有改动，必须先说明，不要覆盖或回退。

## Commit 前必须检查

每次执行提交（git commit）前，必须先向用户展示：

- 当前 Git 状态（git status）总结。
- 当前差异（git diff）总结。
- 准备提交的文件列表。
- 每个文件的变更作用。
- 测试是否通过。
- 如果没有运行测试，必须明确说明“未运行测试”以及原因。

只有用户明确确认后，才能执行 `git commit`。

## Commit Message 建议

可以根据当前差异（diff）生成提交信息（commit message）建议。

提交信息建议应：

- 使用简洁、明确的英文。
- 优先使用 Conventional Commits 风格，例如 `docs:`、`chore:`、`test:`、`fix:`、`feat:`。
- 只描述本次实际提交内容。
- 不夸大范围，不写未实现的功能。

示例：

```text
docs: add Codex workflow skills and agent configuration
```

## 提交流程

建议流程：

1. 查看当前 Git 状态（git status）。
2. 查看当前差异（git diff）。
3. 总结本次变更范围。
4. 说明测试方式和测试结果。
5. 生成提交信息（commit message）建议。
6. 等待用户明确确认。
7. 只暂存用户确认的文件。
8. 执行提交（git commit）。
9. 汇报提交结果。

## 禁止行为

- 不要自动执行 `git push`。
- 不要自动创建远程分支。
- 不要自动创建拉取请求（pull request）。
- 不要执行破坏性命令，例如 `git reset --hard` 或 `git checkout --`，除非用户明确要求并确认风险。
- 不要把未确认的文件加入提交。

