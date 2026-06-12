# 项目级 Codex 规范

## 项目目标

本仓库用于从 0 复刻一个 C++ AI 语音面试系统。复刻目标是学习和重建能力，不是逐行复制旧项目。

目标产品包含：

- C++ 面试问题生成与管理。
- 候选人回答记录、评分、追问和报告。
- 可选 PDF 简历解析。
- 可选 LLM API、实时语音 WebSocket、PortAudio 音频、Qt 桌面 UI。

## 沟通方式

- 默认使用中文回复。
- 关键技术名词在需要时使用“中文解释 + English 原词”。
- 代码注释默认使用中文，方便不依赖 AI 的读者理解代码意图；必要时保留英文技术名词。
- 面向新手解释时优先讲目标、设计原因、数据流、验证方式。

## 学习优先级

- 先保证能运行，再补完整功能。
- 先写 mock，再接真实服务。
- 先命令行程序，再 Qt UI。
- 先单线程和确定性测试，再引入线程、音频、网络。
- 每次只做一个可验证小目标。
- 参考源项目当前 CMake 入口是 Qt GUI；如果为了学习先做 CLI，请明确这是学习脚手架，不要把旧 CLI 入口当作可直接复刻的当前入口。

## 推荐复刻顺序

1. CMake 项目骨架、`main.cpp`、基础测试。
2. `common`：配置、日志、状态枚举、工具函数。
3. `interview`：问题、回答、评分、报告的纯 C++ 逻辑。
4. `services`：LLM/PDF/audio/realtime 的接口和 mock。
5. 真实 LLM HTTP 客户端。
6. PDF 解析适配器。
7. 二进制协议编解码与测试。
8. 对话编排、状态机、事件回调。
9. PortAudio 音频适配器。
10. WebSocket 实时服务适配器。
11. Qt 主窗口、配置对话框、状态与进度显示。
12. README、CI、GitHub PR 流程、打包说明。

## C++ 代码规范

- 使用 C++17 或更高版本，除非当前阶段明确降低要求。
- 头文件统一使用 `#pragma once`。
- 头文件必须自包含；用到的标准库、Qt 类型必须在本文件直接 include，不依赖传递 include。
- 禁止 `using namespace std;`。
- 禁止在全局作用域、命名空间作用域、匿名命名空间中集中写 `using` 声明。
- 项目代码放在具名命名空间中，例如 `interview::common`。
- 单参数构造函数使用 `explicit`。
- 优先 Rule of Zero，只有自己管理资源时再考虑 Rule of Five。
- 输入参数优先 `const T&`，轻量类型按值传递。
- 输出优先返回值，不优先使用输出参数。
- 不返回局部对象的指针或引用。
- 优先 `std::unique_ptr` 表示唯一所有权，避免裸 owning pointer。
- 复杂外部依赖使用接口隔离，方便 mock 和测试。
- 每次新增或修改代码时，都要为新增逻辑、关键边界、测试意图补充中文注释；注释说明“为什么这样做”和“验证什么”，不要只重复代码字面意思。

## 文件与命名

- 文件名使用小写下划线，例如 `interview_session.h`。
- 类型名使用 PascalCase。
- 函数、变量、形参使用 snake_case。
- 类成员变量以 `_` 结尾。
- 常量优先使用 `kNameStyle`。
- 枚举值优先使用 `kValueStyle`。

## 依赖与密钥

- 不提交真实 API Key、App ID、Access Key、App Key、Token、私有 URL。
- 提交 `config.example.json`，本地使用被 `.gitignore` 忽略的 `config.local.json`。
- 不在日志里输出完整 LLM 请求、API key、简历全文、候选人完整回答或服务端鉴权 header。
- HTTPS/WSS 默认必须开启证书校验。只有在本地诊断时才允许临时关闭，并且不能提交。
- 单元测试不能依赖网络、麦克风、扬声器、真实 PDF 系统环境或外部 API。
- 真实 LLM、PDF、音频、WebSocket 检查放到手动集成测试。
- 新增第三方库前说明用途、替代方案、是否必须。
- 新增依赖时同步更新 `vcpkg.json`、`CMakeLists.txt` 和构建说明。

## 并发与 Qt

- 不新增捕获裸 `this` 的 detached thread。
- 每个线程必须有明确启动点、停止点和 join/退出策略。
- WebSocket 读、写、关闭必须有明确线程归属或串行化策略，不能多个线程同时读同一个 socket。
- Qt UI 只能在主线程更新；后台回调进入 UI 必须使用 queued connection 或等价机制。
- 对象析构前必须取消回调或保证回调不会访问已销毁对象。

## 构建与验证

优先使用这些命令，按当前仓库实际情况调整：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

如果存在 `build.py`：

```bash
python3 build.py --config Debug
```

提交前尽量运行：

```bash
clang-format -i <changed-cpp-files>
cmake --build build -j
ctest --test-dir build --output-on-failure
```

如果暂时不能运行某个命令，说明原因和剩余风险。

## Codex 使用规则

- 做项目功能时优先使用 `$rebuild-cpp-interview`。
- 讲解 C++ 概念、复盘代码、出练习时优先使用 `$cpp-learning-mentor`。
- 大改动前先让 Codex 给出短计划。
- 遇到构建依赖、协议、LLM 安全、并发、Qt 生命周期、GitHub PR 时，可以用 `.codex/subagents/` 中的子代理提示词开独立检查。
- 子代理只用于独立复核、测试计划、PR 检查等边界清晰的任务，不让它们同时改同一批文件。
- 每次完成一个目标功能后，根据当前上下文把 Codex 对话标题改成简短、具体、便于回看历史的标题。

## GitHub 提交流程

- 分支命名：`codex/<short-topic>` 或 `learn/<short-topic>`。
- 每次提交只包含一个学习目标或一个功能闭环。
- 提交前查看 `git status --short`、`git diff --stat`、`git diff`。
- Codex 生成的 commit message 默认使用中文，保留 Conventional Commits 类型前缀：
  - `feat: 添加面试会话骨架`
  - `test: 覆盖配置校验`
  - `docs: 补充复刻阶段说明`
  - `refactor: 抽取 LLM 客户端接口`
- PR 描述必须包含：目标、主要改动、验证结果、未完成事项。

## 禁止事项

- 不要为了统一风格做大规模无收益重构。
- 不要把旧项目大段源码直接复制进来。
- 不要把真实密钥写进代码、测试、截图、日志或提交历史。
- 不要在没有测试或手动验证说明的情况下接入复杂外部服务。
- 不要让 UI 直接依赖具体网络或音频实现。
