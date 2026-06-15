# 2026-06-15 LLM Provider Factory 收口

## 本次目标

本次把 “根据配置决定创建哪个 LLM 客户端” 的职责，从 `main` 入口函数收口到 `services` 层。

这样做的原因是：当前仓库虽然已经有 `ILlmClient` 和 `MockLlmClient`，但 `main` 仍然直接知道具体实现类型并自己判断 provider。后续一旦接入真实 `HttpLlmClient`，这种写法会让入口层越来越胖，也会让 UI 或其他入口重复同样的分支。

## 修改了什么

- `include/services/llm_client_factory.h`、`src/services/llm_client_factory.cpp`
  - 新增 `createLlmClient(const common::LlmConfig&)`。
  - 由 `services` 层统一负责 provider 解析和客户端创建。
- `src/main.cpp`
  - 删除匿名命名空间里的本地 `createLlmClient(...)`。
  - 改为调用 `services::createLlmClient(config.llm)`。
- `CMakeLists.txt`
  - 把新的 factory 源文件纳入 `services_lib`。
- `test/services/test_llm_client_factory.cpp`
  - 新增工厂测试，覆盖 `mock` provider 成功创建和不支持 provider 抛错。
- `test/CMakeLists.txt`
  - 把新的 factory 测试接入测试目标。

## 核心数据流

1. `main` 读取 `AppConfig`。
2. `main` 把 `config.llm` 交给 `services::createLlmClient(...)`。
3. `services` 层根据 `provider` 返回具体的 `ILlmClient` 实现。
4. `main` 拿到抽象后的 `ILlmClient`，继续调用 `prepareInterview(...)`。
5. `session` 和 `app` 层不需要知道底层到底是 mock 还是未来的真实 HTTP 客户端。

## 关键 C++ 知识点

- 工厂函数（Factory Function）
  - 用一个统一入口负责“根据配置选择具体实现”，可以把对象创建逻辑和业务编排逻辑分开。
- 组合根（Composition Root）
  - `main` 仍然负责把配置、日志、服务和流程组装起来，但不再直接持有 provider 解析细节。
- 面向接口编程（Program to Interface）
  - `main`、`session`、`app` 都只依赖 `ILlmClient`，为后续增加真实实现留出了稳定接入点。

## 验证结果

- 编译：`cmake --build build -j` 通过。
- 测试：`ctest --test-dir build --output-on-failure` 通过，共 `68` 个测试。
- 新增验证：
  - `LlmClientFactoryTest.CreatesMockClientForMockProvider`
  - `LlmClientFactoryTest.ThrowsForUnsupportedProvider`

## 注释自检

- 新增 public API `createLlmClient(...)` 已补中文注释，说明为什么要把创建逻辑收口到 `services`。
- 新增测试用例前已补中文注释，说明验证的是哪条边界以及为什么重要。
- `main` 中新增注释说明了“入口层只做组装”的设计意图。

## 抽象是否接入主流程

这次新增的 `LLM provider factory` 抽象已经接入主流程。

`src/main.cpp` 现在直接调用 `services::createLlmClient(config.llm)`，因此它不是悬空接口，而是当前 CLI 启动路径的真实创建入口。

## 下一步建议

下一步进入真实 LLM 客户端骨架，但仍保持默认测试离线：

- 扩展 `LlmConfig`，补真实客户端所需字段。
- 新增 `HttpLlmClient` 类定义和最小错误处理路径。
- 把真实网络验证放到手动集成检查，不放进单元测试。
