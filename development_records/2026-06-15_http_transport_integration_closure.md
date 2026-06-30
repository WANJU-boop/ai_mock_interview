# 2026-06-15 HTTP transport 接通闭环

## 本次目标

本次把 `http` provider 从“类型已经接到主流程、但运行时一定失败”的中间状态，收口成“默认会注入真实 transport、可以发起真实 HTTPS 请求”的最小闭环。

这样做的原因是：上一阶段的 `HttpLlmClient` 已经把请求体构造、结构化 JSON 解析和离线 fake transport 测试固定下来，但 `services::createLlmClient(...)` 仍然会返回一个没有 transport 的半成品客户端。继续往 PDF、Qt 或并发模块扩展之前，先把这个最明显的架构断点补通，主流程才算真正具备真实 LLM provider 的最小可用路径。

## 修改了什么

- `include/services/beast_http_transport.h`、`src/services/beast_http_transport.cpp`
  - 新增基于 Boost.Beast + OpenSSL 的同步 HTTPS `IHttpTransport` 实现。
  - 负责解析 `https://` URL、设置 SNI、开启证书校验、发送 JSON POST 并返回最小响应结构。
- `src/services/llm_client_factory.cpp`
  - `http` provider 现在会直接注入 `BeastHttpTransport`，不再返回缺 transport 的客户端。
- `include/services/http_llm_client.h`、`src/services/http_llm_client.cpp`
  - 构造函数改成必须显式注入 `IHttpTransport`。
  - 在客户端构造阶段补 `https://` 校验和非空 transport 校验，让 factory 和直接构造都能尽早失败。
- `src/common/config.cpp`
  - 配置层新增 `llm.base_url` 必须以 `https://` 开头的校验。
- `test/common/test_config.cpp`
  - 补 `api_key_env` 缺失、非 HTTPS base URL 的配置边界测试。
- `test/services/test_http_llm_client.cpp`
  - 补 transport 缺失、坏 JSON、无 `choices` 包装、空题目、越界 score、缺反馈、URL 拼接边界等测试。
- `test/services/test_llm_client_factory.cpp`
  - 补非法 HTTP 配置在 factory 创建阶段直接失败的测试。
- `CMakeLists.txt`
  - 把 `beast_http_transport.cpp` 接入 `services_lib`，补 `boost_asio`、`boost_beast`、`boost_system` 和 OpenSSL 链接。

## 核心数据流

1. `main` 读取配置。
2. `services::createLlmClient(config.llm)` 根据 provider 创建具体客户端。
3. 当 provider 为 `http` 时：
   - factory 创建 `BeastHttpTransport`。
   - factory 用该 transport 构造 `HttpLlmClient`。
   - `HttpLlmClient` 继续负责领域请求到 OpenAI 兼容 JSON 的转换，以及结构化响应解析。
   - `BeastHttpTransport` 负责真实 HTTPS POST、TLS 握手和响应正文读取。
4. `prepareInterview(...)` 和 CLI 主流程不需要知道底层 transport 细节，仍只依赖 `ILlmClient`。

## 关键 C++ 知识点

- 依赖注入（Dependency Injection）
  - `HttpLlmClient` 不再隐式创建 transport，而是显式接收 `IHttpTransport`，这样 fake transport 测试和真实 transport 集成都能共用同一客户端逻辑。
- 适配器模式（Adapter Pattern）
  - `BeastHttpTransport` 把 Boost.Beast/OpenSSL 的网络细节压到服务层边界内，上层只看到 `HttpRequest` / `HttpResponse`。
- Fail Fast
  - `https://` 和非空 transport 的校验前移到配置层与客户端构造阶段，避免把错误拖到真正联网时才暴露。
- RAII 与同步 I/O 生命周期
  - `io_context`、`ssl::context`、`ssl_stream`、HTTP request/response 都由栈对象管理，当前最小闭环不需要额外手动释放。

## 验证结果

- `cmake --build build -j` 通过。
- `ctest --test-dir build --output-on-failure` 通过，共 `91` 个测试。
- 重点回归：
  - `ConfigTest.ThrowsWhenHttpProviderUsesNonHttpsBaseUrl`
  - `HttpLlmClientTest.ThrowsWhenTransportIsNotInjected`
  - `HttpLlmClientTest.ThrowsWhenResponseBodyIsMalformedJson`
  - `LlmClientFactoryTest.ThrowsWhenHttpProviderConfigIsInvalid`

## 手动集成验证建议

当前默认测试仍然不联网。要做真实请求验证，建议用本地忽略文件提供真实配置和环境变量，然后手动运行：

```bash
OPENAI_API_KEY=your_real_key ./build/AI_mock_interview path/to/config.local.json
```

其中 `config.local.json` 的 `llm.provider` 设为 `http`，`llm.base_url` 保持 `https://api.openai.com/v1` 这类 HTTPS 地址。

## 注释自检

- 新增 public API `BeastHttpTransport` 已补中文注释，说明它为什么存在以及负责哪一层边界。
- `HttpLlmClient` 构造约束改动已补中文注释，解释为什么要禁止半成品客户端进入主流程。
- 新增测试用例前都补了中文注释，重点说明验证哪个边界以及为什么重要。

## 抽象是否接入主流程

这次新增的真实 HTTP transport 抽象已经真正接入主流程。

`src/services/llm_client_factory.cpp` 在 `provider == "http"` 时会直接构造并注入 `BeastHttpTransport`，所以它不是悬空接口，而是当前 CLI 启动路径下真实 provider 的默认 transport。

## 剩余风险和下一步建议

- 当前真实 transport 只实现了同步 HTTPS POST，还没有重试、代理、自定义 CA、取消控制和更细粒度的错误分类。
- CLI 仍然会把完整回答和报告打印到 stdout，这个安全收口还没做。

下一步建议优先只做一件事：

- 给真实 HTTP provider 增加一次受控的手动集成检查入口，并补最小的错误分类/用户提示策略。

这样可以在不破坏现有离线单测的前提下，把“请求超时、证书错误、401/429”等真实失败路径说明得更清楚。
