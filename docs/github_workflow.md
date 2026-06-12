# GitHub 提交流程

## 分支

推荐两类分支：

```text
learn/<milestone-name>
codex/<feature-name>
```

示例：

```text
learn/cmake-skeleton
codex/config-loader
codex/mock-llm-client
```

## 本地提交前检查

```bash
git status --short
git diff --stat
git diff
cmake --build build -j
ctest --test-dir build --output-on-failure
```

如果 build 或 test 暂时不能运行，在 PR 描述里写清楚原因。

## Commit Message

使用简单的 Conventional Commits。Codex 生成的 commit message 默认使用中文，保留类型前缀：

```text
feat: 添加面试会话骨架
test: 覆盖配置校验
docs: 补充复刻学习路线
refactor: 抽取 LLM 客户端接口
fix: 处理缺失配置字段
```

## PR 内容

PR 描述必须包含：

- 目标：这次解决什么。
- 改动：主要改了哪些模块。
- 验证：运行过哪些命令。
- 风险：哪些地方还没验证。
- 下一步：下一个学习小目标。

## Review 重点

- 是否提交了密钥、本地配置、日志、报告。
- 是否把真实网络调用放进默认单元测试。
- 是否有不必要的大重构。
- 是否违反头文件规范和命名空间规则。
- 是否有并发关闭顺序问题。
- 是否能用一个命令复现验证结果。

## 发布节奏

新手学习阶段建议一个 milestone 一个 PR。不要等十几个文件都写完才提 PR。
