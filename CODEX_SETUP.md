# Codex Setup

这是一套可以搬到新项目根目录的 Codex 学习开发启动包。

## 放置方式

把本目录里的文件复制到你的新项目根目录：

```text
new-project/
├── AGENTS.md
├── .codex/
├── .github/
├── docs/
├── .clang-format
└── .gitignore
```

## 推荐用法

1. 新建空仓库。
2. 复制这些文件。
3. 初始化 Git。
4. 从 `docs/rebuild_learning_path.md` 的第 0 阶段开始。
5. 对 Codex 说：`Use $rebuild-cpp-interview to implement milestone 0.`

## Skills

本启动包包含两个技能：

- `$rebuild-cpp-interview`：用于规划和实现复刻项目。
- `$cpp-learning-mentor`：用于把每一步改动讲成 C++ 学习任务。

如果你的 Codex 版本不会自动发现项目内 `.codex/skills`，把两个技能目录复制到全局技能目录：

```text
~/.codex/skills/rebuild-cpp-interview
~/.codex/skills/cpp-learning-mentor
```

## Subagents

`.codex/subagents/` 里是子代理提示词模板。它们不是项目源码，适合在 Codex 支持子代理或新线程时复制使用。

建议使用方式：

- 主线程负责编码。
- 子代理负责架构复核、测试计划、并发风险、PR 检查。
- 不要让多个子代理同时改同一批文件。

## GitHub

`.github/pull_request_template.md` 是 PR 模板。

`.github/workflows/ci.yml` 是早期 CMake CI 模板，适合还没有接入 Qt、PortAudio、PoDoFo 等重依赖的阶段。接入真实依赖后，需要补 vcpkg 缓存和系统库安装。

## Current Limitation

本启动包生成时尝试读取官方 Codex manual，但当前环境访问官方站点失败。因此这里采用当前会话可用能力、本地技能规范和项目源码反推配置。后续如果你的 Codex 文档有更新，以官方文档为准。
