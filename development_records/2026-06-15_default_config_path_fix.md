# 2026-06-15 默认配置路径启动修复

## 本次目标

本次修复“从项目根目录外直接启动 `build/AI_mock_interview` 时，程序找不到 `config.example.json`”的问题。

这样做的原因是：当前 CLI 程序默认使用相对路径读取配置文件，这在开发者已经 `cd` 到项目根目录时没问题，但从 Finder、其他终端目录或绝对路径直接启动时，就会误报“程序跑不起来”。

## 修改了什么

- `include/common/config.h`、`src/common/config.cpp`
  - 新增 `findDefaultConfigPath(...)`。
  - 先保持当前工作目录优先，再回退到可执行文件目录及其上级目录查找 `config.example.json`。
- `src/main.cpp`
  - 默认配置路径改为调用 `findDefaultConfigPath(argv[0])`。
- `test/common/test_config.cpp`
  - 新增三条测试，覆盖：
    - 当前目录优先
    - 从 `build/AI_mock_interview` 回退到项目根目录
    - 完全找不到配置时继续返回旧文件名

## 核心数据流

1. `main` 启动时，如果用户显式传了配置路径，继续直接使用。
2. 如果用户没传配置路径：
   - 先检查当前工作目录下是否有 `config.example.json`
   - 没有时，再沿着可执行文件所在目录向上查找
3. 找到配置后，继续走原有的 `loadConfigFromFile(...) -> createLlmClient(...) -> prepareInterview(...)` 主流程。

## 关键 C++ 知识点

- `std::filesystem`
  - 用于路径拼接、存在性检查和目录层级回退。
- 启动容错（Startup Fallback）
  - 对用户最常见的误用场景做最小回退，比一开始就要求用户理解工作目录更实用。
- 保持兼容
  - 当前目录已有配置时继续返回相对路径，避免破坏已有脚本和命令行习惯。

## 验证结果

- 待本轮修复后运行：
  - `cmake --build build -j`
  - `ctest --test-dir build --output-on-failure`

## 注释自检

- 新增 public API `findDefaultConfigPath(...)` 已补中文注释。
- 新增测试均说明了验证目标和边界意义。
- 路径回退逻辑补了中文注释，说明为什么要向可执行文件上级目录查找。

## 抽象是否接入主流程

这次新增的默认配置路径解析已经接入主流程。

`src/main.cpp` 在未显式传配置文件时，会直接调用 `findDefaultConfigPath(argv[0])`，因此它不是辅助工具，而是当前 CLI 启动路径的真实默认行为。

## 下一步建议

下一步优先继续把 CLI 启动体验收口：

- 在启动时打印当前实际使用的配置文件路径
- 为 `question_count=3` + 追问场景补更友好的交互提示
- 再决定是否增加 `config.local.json` 优先级
