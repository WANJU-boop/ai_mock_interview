# 开发与提交说明

项目规范以根目录 `AGENTS.md` 为准。每次只完成一个可验证学习目标，
先 mock、再真实服务；新增 public API、边界逻辑和测试意图补中文注释。

## 本地验证

```sh
clang-format -i <本次修改的 C++ 文件>
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
git diff --check
git status --short
git diff --stat
git diff
```

clang-format 后复查 include 顺序。当前配置会重新分组，必要时用局部
`clang-format off/on` 保留“对应头文件 → 标准库 → 第三方 → 项目”的分组。
macOS Qt 报告测试会短暂显示窗口，需在可访问图形会话的环境中运行；
Linux CI 使用 `QT_QPA_PLATFORM=offscreen`。

## 提交约定

- 功能分支使用 `codex/<topic>` 或 `learn/<topic>`。
- 中文 commit message 保留 Conventional Commits 前缀，例如
  `feat: 添加报告预览`、`fix: 修复日志初始化失败后的崩溃`。
- 不把无关副本、真实配置、日志或候选人报告加入提交。
- PR 说明目标、改动、验证结果和未完成事项。

## 历史提交规范截图

原首页的提交规范截图保留在这里，项目首页优先展示软件效果和首次运行方式。

![历史提交规范](../README.assets/image-20260303095556659.png)
