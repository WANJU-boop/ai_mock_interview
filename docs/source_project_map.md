# 源项目结构速览

当前参考项目是一个 C++/Qt AI 语音面试系统。README 为空，所以结构主要来自代码和构建文件。

## 构建

- `CMakeLists.txt`：C++17、Qt automoc、vcpkg、OpenSSL、Boost、nlohmann-json、ZLIB、PortAudio、CURL、PoDoFo、spdlog、Qt。
- `build.py`：跨平台构建包装脚本，检查 Python、CMake、编译器、vcpkg，然后配置和编译。
- `vcpkg.json`：声明第三方依赖和版本覆盖。
- 当前 CMake 目标使用 `src/main_qt.cpp` 作为入口。
- `src/main.cpp` 更像旧 CLI 入口或遗留入口，依赖和命名空间与当前 CMake 目标不完全一致；从 0 复刻时不要把它当作可直接复制的主入口。

## 主要模块

- `include/common` 与 `src/common`
  - `config`：读取 JSON 配置，生成 realtime session 请求。
  - `logger`：日志封装。
  - `protocol`：二进制协议编解码。
  - `interview_state`：全局状态机。

- `include/services` 与 `src/services`
  - `llm_client`：OpenAI 兼容 LLM HTTP 调用。
  - `pdf_parser`：PDF 文本提取。
  - `audio_manager`：PortAudio 麦克风和扬声器。
  - `realtime_client`：实时语音服务 WebSocket。

- `include/interview` 与 `src/interview`
  - `interview_manager`：问题生成、答案记录、评分、追问、报告。
  - `dialog_session`：协调音频、WebSocket、状态机和面试流程。

- `include/ui` 与 `src/ui`
  - `mainwindow`：Qt 主窗口，展示状态、进度、对话。
  - `config_dialog`：新建会话配置。

## 新手风险点

- 依赖太多，一次性装齐和接通会很慢。
- 配置中有服务凭据字段，不能提交真实密钥。
- 日志不能输出完整请求体、简历全文、候选人完整回答或鉴权 header。
- HTTPS/WSS 证书校验不能为了省事长期关闭。
- 音频、WebSocket、Qt 都涉及线程，容易出现关闭顺序和 UI 线程问题。
- 不要新增 detached thread 捕获对象裸 `this`。
- WebSocket 读、写、关闭要有明确线程归属，避免多个线程同时读同一个 socket。
- 头文件要自包含，不依赖其他文件顺带 include。
- LLM 和实时服务不适合放进默认单元测试。
- 协议解析必须用测试覆盖边界，否则网络错误很难定位。
- TTS/PCM 音频格式要与服务端实际返回核对，不能只看本地配置字段推断。

## 复刻策略

先复刻“领域逻辑”，再逐步接入“外部能力”。每个外部能力都先写接口和 mock，再写真实实现。
