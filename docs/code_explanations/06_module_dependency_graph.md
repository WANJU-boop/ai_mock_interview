# 文字版模块依赖图

## 总体依赖方向

这个项目的依赖方向可以先记成一句话：

```text
入口和应用层调用业务层，业务层通过接口调用服务层，服务层复用 common 底座。
```

文字图如下：

```text
main executables
  -> app
    -> session
      -> services
        -> common

test
  -> app / session / services / common
```

更贴近代码的图：

```text
src/main.cpp
  -> app_lib
  -> session_lib
  -> services_lib
  -> common_lib

app_lib
  -> session_lib
  -> services_lib
  -> common_lib

session_lib
  -> services_lib 的接口类型
  -> common_lib

services_lib
  -> common_lib
  -> Boost.Beast / Boost.Asio / OpenSSL / PoDoFo / nlohmann-json

common_lib
  -> spdlog
  -> nlohmann-json
```

## CMake target 视角

当前 `CMakeLists.txt` 里有四个核心库：

```text
common_lib
  被 services_lib、session_lib、app_lib、主程序使用

services_lib
  被 app_lib 和主程序使用
  提供 LLM、HTTP、PDF、Realtime、火山协议能力

session_lib
  被 app_lib 使用
  提供面试领域逻辑和报告

app_lib
  被 AI_mock_interview 和 AI_mock_interview_realtime_demo 使用
  提供 CLI / demo 级流程
```

可执行程序：

```text
AI_mock_interview
  -> app_lib
  -> common_lib
  -> services_lib

AI_mock_interview_realtime_demo
  -> app_lib
  -> common_lib
  -> services_lib

AI_mock_interview_volc_text_demo
  -> services_lib
```

## 源码目录视角

```text
include/common + src/common
  <- include/services + src/services
  <- include/session + src/session
  <- include/app + src/app
  <- src/main*.cpp
```

箭头 `<-` 的意思是“被右边依赖”。

也就是说：

- `common` 最底层，尽量不要依赖其他项目模块。
- `services` 可以依赖 `common`，但不应该依赖 `app` 或 UI。
- `session` 可以依赖服务接口，但不应该依赖具体 WebSocket/HTTP/PDF 实现细节。
- `app` 可以组装业务流程，但不要沉淀复杂领域规则。
- `main` 只做启动、依赖创建和错误码返回。

## Realtime/WebSocket 专门依赖图

```text
DialogOrchestrator
  -> IRealtimeClient
    -> MockRealtimeClient
    -> VolcRealtimeClientAdapter
      -> VolcRealtimeClient
        -> IVolcRealtimeTransport
          -> BeastVolcRealtimeTransport
        -> VolcRealtimeProtocol
      -> common::RealtimeEvent
```

这张图的重点是：`DialogOrchestrator` 只依赖 `IRealtimeClient`，不依赖 `VolcRealtimeClient`，更不依赖 Boost.Beast。

## LLM 专门依赖图

```text
InterviewManager
  -> ILlmClient
    -> MockLlmClient
    -> HttpLlmClient
      -> IHttpTransport
        -> BeastHttpTransport
```

这张图的重点是：评分逻辑只通过 `ILlmClient` 调用，测试可以使用 `MockLlmClient`，真实 provider 才走 HTTP。

## PDF 专门依赖图

```text
prepareInterview
  -> IPdfParser
    -> MockPdfParser
    -> PodofoPdfParser
      -> PoDoFo
```

这张图的重点是：简历解析只发生在启动准备阶段，面试主循环不用知道 PDF 文件怎么读。

## 新手理解提示

C++ 里“依赖”通常体现在三件事：

1. `#include`：一个文件需要知道另一个类型或函数。
2. CMake `target_link_libraries`：一个 target 链接另一个库。
3. 构造函数参数：例如 `InterviewManager(..., ILlmClient& llm_client)`，表示运行时需要注入一个 LLM 能力。

读这个项目时，可以按这个顺序判断依赖是否合理：

```text
这个模块是不是只依赖它真正需要知道的抽象？
这个模块有没有越过接口直接依赖具体外部实现？
这个依赖会不会让单元测试必须联网、读真实 PDF 或需要真实账号？
```

如果答案让测试变复杂，通常就需要加接口或 adapter。
