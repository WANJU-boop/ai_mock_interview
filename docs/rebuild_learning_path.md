# 从 0 复刻学习路线

## 总原则

每一阶段都要产出一个能运行或能测试的小结果。不要一开始就接入 Qt、音频、WebSocket 和真实 LLM。

参考源项目当前实际入口是 Qt GUI；本路线先从 CLI 和纯 C++ 逻辑开始，是为了降低新手学习成本。到阶段 10 再切回 Qt UI 形态。

## 阶段 0：空项目能编译

目标：创建 CMake C++17 项目，输出一行程序版本。

学习点：

- CMake 基础。
- `src/`、`include/`、`tests/` 的目录意义。
- Debug 构建和编译错误阅读。

建议 Codex 提示：

```text
Use $rebuild-cpp-interview to implement milestone 0. Keep it beginner-friendly and add the smallest possible CMake project.
```

验证：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/<your-executable>
```

## 阶段 1：配置和日志

目标：读取 `config.example.json`，校验字段，输出日志。

学习点：

- 头文件和源文件分离。
- `nlohmann::json`。
- 异常处理。
- 不提交本地密钥。

验证：缺字段、错类型、合法配置都要有测试。

## 阶段 2：面试领域模型

目标：不用 AI，也能完成“问题列表 -> 回答 -> 分数 -> 报告 JSON”。

学习点：

- `struct` 和 `class` 的选择。
- `std::vector`、`std::string`。
- 单一职责。
- 可测试设计。

验证：测试下一题、记录回答、完成状态、报告内容。

## 阶段 3：LLM 接口和 mock

目标：定义 `ILlmClient`，先用 `MockLlmClient` 返回固定问题和评分。

学习点：

- 接口隔离。
- 依赖注入。
- 为什么单元测试不应该调用真实网络。

验证：领域测试只依赖 mock。

## 阶段 4：真实 LLM 客户端

目标：用 libcurl 或其他 HTTP 库调用 OpenAI 兼容接口。

学习点：

- HTTP POST。
- JSON 请求和响应。
- 超时、错误码、解析失败。
- API key 本地配置。

验证：保留手动集成命令，不放入默认 CI。

## 阶段 5：PDF 简历解析

目标：定义 `IPdfParser`，先用假文本，再接真实 PDF 库。

学习点：

- 文件 I/O。
- UTF-8 文本。
- 第三方库适配器。

验证：普通文本或 mock parser 测试问题生成。

## 阶段 6：协议编解码

目标：实现 realtime 协议的 header、payload、event 解析。

学习点：

- 二进制数据。
- 字节序。
- `enum class`。
- 边界测试。

验证：用固定 byte fixture 测试 parser。

## 阶段 7：对话编排

目标：用 mock realtime client 模拟 TTS、ASR、用户说话事件，驱动状态机。

学习点：

- 回调。
- 状态转换。
- 队列。
- 线程关闭顺序。

验证：事件序列测试，例如 TTS_START -> TTS_END -> ASR_RESULT。

## 阶段 8：音频

目标：PortAudio 适配器能打开设备，短录短放。

学习点：

- RAII。
- 阻塞 I/O。
- 采样率、声道、PCM。
- 设备权限。

验证：手动 smoke test，失败时输出清晰错误。

## 阶段 9：WebSocket 实时服务

目标：真实连接服务，发送文本或音频，接收事件。

学习点：

- TLS/WebSocket。
- 鉴权 header。
- 后台接收线程。
- 网络错误恢复。
- socket 读写关闭的线程归属。

验证：手动集成测试，不进入默认单元测试。

## 阶段 10：Qt UI

目标：主窗口能配置会话、开始面试、显示状态和对话。

学习点：

- Qt signals/slots。
- 主线程 UI 更新。
- worker thread。
- UI 状态来自状态机。
- 对象销毁前取消回调。

验证：人工检查窗口、按钮状态、日志、错误弹窗。

## 阶段 11：工程化

目标：补 README、CI、PR 模板、发布说明。

学习点：

- Git 分支。
- 小提交。
- CI。
- PR review。

验证：CI 或本地完整构建通过。
