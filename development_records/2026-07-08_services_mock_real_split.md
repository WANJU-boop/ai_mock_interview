# Services Mock 与真实实现分层

## 本次目标

把 `services` 从平铺文件改成按能力和 provider 分层的目录结构，让接口、mock、HTTP、PoDoFo、火山 realtime 实现各自有清晰位置。

本次只做结构迁移和声明拆分，不重写函数体里的业务逻辑：mock 评分规则、HTTP 请求构造、PoDoFo 文本提取、火山协议编解码和 adapter 映射行为都保持原样。

## 新目录边界

```text
include/services/
  llm/        # ILlmClient、mock LLM、HTTP LLM 和 HTTP transport
  pdf/        # IPdfParser、mock PDF、PoDoFo PDF
  realtime/   # IRealtimeClient、mock realtime、火山 realtime provider
```

```text
src/services/
  llm/
  pdf/
  realtime/
```

## 核心数据流

LLM：

```text
ILlmClient
  -> mock/MockLlmClient
  -> http/HttpLlmClient + http/IHttpTransport
```

PDF：

```text
IPdfParser
  -> mock/MockPdfParser
  -> podofo/PodofoPdfParser
```

Realtime：

```text
IRealtimeClient
  -> mock/MockRealtimeClient
  -> volc/VolcRealtimeClientAdapter
  -> volc/VolcRealtimeClient
  -> volc/IVolcRealtimeTransport
  -> volc/BeastVolcRealtimeTransport
```

## 关键 C++ 知识点

- 接口头文件只暴露稳定抽象，避免调用方无意依赖 mock 或真实第三方实现。
- mock 和真实 provider 分目录后，测试 include 会更明确：测试要用 mock，就显式包含 `mock/...`。
- `IHttpTransport` 和 `IVolcRealtimeTransport` 单独成头文件，说明它们是传输边界，不是业务 client 本身。
- CMake 仍保留一个 `services_lib`，先降低迁移风险；未来只有在链接边界真的变复杂时再拆 target。

## 注释自检

- 新增的 mock/provider 头文件都保留了 public API 中文注释。
- 本次没有改动函数体逻辑，因此原有 mock 行为、错误处理和协议边界注释继续保留。
- 新增 `volc_realtime_transport.h` 后，transport 的连接、发送、接收、关闭职责有独立说明。

## 验证结果

已运行：

```bash
clang-format -i <本次改动的 C++ 文件>
cmake --build build -j
ctest --test-dir build --output-on-failure
```

结果：

- 构建通过。
- 全量测试 `155/155` 通过。
- 第一次普通构建被沙箱阻止访问本机 vcpkg 文件锁；授权后构建通过。

## 下一步建议

如果继续整理，可以把 `services_lib` 再拆成更细的 CMake target，例如 `services_llm_lib`、`services_pdf_lib`、`services_realtime_lib`。但当前阶段不建议马上拆，先让目录分层稳定一轮，避免同时改变文件组织和链接边界。
