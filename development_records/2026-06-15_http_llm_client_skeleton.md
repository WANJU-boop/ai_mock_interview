# 2026-06-15 HttpLlmClient 骨架闭环

## 本次目标

本次进入里程碑 4 的第一个最小切片：把真实 LLM provider 的配置边界、客户端类型和主流程接线先补齐，但暂时不直接接真实网络。

这样做的原因是：当前仓库的 mock CLI 面试闭环已经稳定，如果现在直接把 Boost.Beast、TLS、超时、错误恢复一次全接进来，调试面会太大。先把 `HttpLlmClient` 的输入输出边界、JSON 形状和离线测试固定下来，后面再接真实传输会稳很多。

## 修改了什么

- `include/common/config.h`、`src/common/config.cpp`
  - 扩展 `LlmConfig`，新增 `base_url`、`api_key_env`、`timeout_ms`。
  - 当 provider 为 `http` 时，在配置层提前校验关键字段。
- `include/services/http_llm_client.h`、`src/services/http_llm_client.cpp`
  - 新增 `HttpLlmClient`、`IHttpTransport`、`HttpRequest`、`HttpResponse`。
  - 支持请求体构造、鉴权 header 组装、结构化 JSON 响应解析和错误路径收口。
- `include/services/llm_client_factory.h`、`src/services/llm_client_factory.cpp`
  - `createLlmClient(...)` 新增 `http` provider 分支。
- `test/common/test_config.cpp`
  - 补真实 provider 配置加载与错误边界测试。
- `test/services/test_llm_client_factory.cpp`
  - 补 `http` provider 工厂创建测试。
- `test/services/test_http_llm_client.cpp`
  - 通过 fake transport 离线验证请求构造、响应解析、缺密钥和坏状态码路径。
- `CMakeLists.txt`、`test/CMakeLists.txt`
  - 把新客户端和新测试接入构建。
- `config.example.json`
  - 补真实 provider 预期字段结构，但默认 provider 仍保持 `mock`。

## 核心数据流

1. `main` 读取 `AppConfig`。
2. `services::createLlmClient(config.llm)` 根据 provider 选择 `MockLlmClient` 或 `HttpLlmClient`。
3. 当 provider 为 `http` 时：
   - `HttpLlmClient` 读取 `model/base_url/api_key_env/timeout_ms`。
   - `generateQuestions(...)` 或 `scoreAnswer(...)` 把领域请求转换成 OpenAI 兼容 JSON。
   - 客户端通过 `IHttpTransport` 发送 HTTP JSON POST。
   - 返回的 JSON 再被解析成当前项目稳定的 `questions` 数组或 `LlmScoreResult`。

## 关键 C++ 知识点

- 接口隔离（Interface Isolation）
  - `IHttpTransport` 让“HTTP 发送”和“LLM 语义转换”分开，单元测试无需网络。
- 依赖注入（Dependency Injection）
  - `HttpLlmClient` 接收可注入 transport，测试可以用 fake 精准验证请求体和错误边界。
- 结构化输出约束（Structured Output Contract）
  - 先强约束返回 JSON 形状，后续 CLI、报告和 UI 都不用解析自由文本。
- 配置前置校验（Fail Fast）
  - `http` provider 的关键字段在加载配置时就报错，减少运行到半程才失败的情况。

## 验证结果

- 待本轮实现后运行：
  - `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
  - `cmake --build build -j`
  - `ctest --test-dir build --output-on-failure`

## 注释自检

- 新增 public API `HttpLlmClient`、`IHttpTransport`、`HttpRequest`、`HttpResponse` 已补中文注释。
- 新增测试用例前都补了中文注释，说明验证目标和边界意义。
- `.cpp` 里对结构化 JSON、配置前置校验和传输注入原因补了中文注释。

## 抽象是否接入主流程

这次新增的 `HttpLlmClient` 类型已经接入主流程的创建路径。

`src/services/llm_client_factory.cpp` 现在可以根据 `provider == "http"` 返回 `HttpLlmClient`。不过默认传输层尚未接真实实现，所以它当前属于“类型和边界已接通、真实网络尚未接通”的中间闭环。

## 下一步建议

下一步优先只做一件事：

- 给 `IHttpTransport` 增加基于 Boost.Beast/OpenSSL 的真实实现，并做一次手动集成验证。

这样可以复用本次已经固定好的请求体、响应解析和离线测试，不需要再回头改上层流程。
