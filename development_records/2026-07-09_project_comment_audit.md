# 全项目 C++ 注释审计与补强

## 本次目标

以 `src/services/llm/http/beast_http_transport.cpp` 的说明层次为参考，审计项目全部 C++ 头文件、实现和测试，并补齐影响学习与维护的注释。

审计不是按注释行数机械判断，而是检查代码能否回答以下问题：

- public API 的用途、调用时机、阻塞行为和失败方式是什么。
- 外部服务的资源由谁拥有，密钥从哪里注入，默认测试为什么不能联网。
- 二进制协议的字段顺序、字节序、长度边界和严格失败策略是什么。
- 状态机如何从输入事件推进到评分、追问、报告和资源关闭。
- mock/fake 模拟了哪些真实约束，每个测试用例验证什么关键边界。

## 审计结果

本次共检查 74 个 C++ 文件：

- 29 个头文件。
- 26 个实现文件。
- 19 个测试文件。

其中 55 个文件已有足够的设计、边界或测试意图说明，不重复添加字面注释。以下 19 个文件存在明确缺口并已补强。

### public API 与公开数据结构

- `include/common/config.h`
- `include/common/realtime_protocol.h`
- `include/services/llm/http/beast_http_transport.h`
- `include/services/llm/http/http_llm_client.h`
- `include/services/llm/http/http_transport.h`
- `include/services/llm/mock/mock_llm_client.h`
- `include/services/pdf/mock/mock_pdf_parser.h`
- `include/services/pdf/podofo/podofo_pdf_parser.h`
- `include/services/realtime/mock/mock_realtime_client.h`
- `include/services/realtime/volc/beast_volc_realtime_transport.h`
- `include/session/dialog_orchestrator.h`
- `include/session/dialog_session.h`
- `include/session/interview_manager.h`

### 非平凡实现与边界处理

- `src/common/config.cpp`
- `src/common/realtime_protocol.cpp`
- `src/services/realtime/mock/mock_realtime_client.cpp`
- `src/session/dialog_orchestrator.cpp`
- `src/session/interview_setup.cpp`

### 测试意图

- `test/common/test_config.cpp`

## 核心数据流

```text
不可信 JSON
  -> 字段类型/范围/provider 安全校验
  -> AppConfig
  -> PDF/LLM 面试准备
  -> PreparedInterview
  -> realtime 事件
  -> DialogOrchestrator 状态转换
  -> 评分/追问/QuestionAnswerRecord
  -> 报告与客户端关闭
```

内部 realtime frame 的注释同时锁定了协议顺序：

```text
type(1 byte)
  -> text length(4)
  -> error length(4)
  -> payload length(4)
  -> text bytes
  -> error bytes
  -> payload bytes
```

## 关键 C++ 知识点

- public API 注释应说明契约、所有权和失败方式，而不是复述函数名。
- `std::unique_ptr` 表示 `PreparedInterview` 独占管理器，引用成员表示外部对象必须活得更久。
- 二进制长度使用固定宽度整数和大端序，编码前防止 `size_t` 截断，解码前检查剩余字节。
- RAII 用于测试中恢复工作目录，避免断言失败后污染其它用例。
- mock 应复刻真实对象的生命周期约束，而不只是返回固定成功值。

## 注释自检

- 新增或补强的 public API 已说明用途、阻塞/异常或生命周期约束。
- 配置、HTTP、PDF 和 WebSocket 边界已说明真实密钥、敏感正文和默认离线测试要求。
- 协议代码已说明 header 布局、字节序、字段顺序和坏包处理。
- 状态机已说明临时记录、追问落盘时机、失败收口和连接关闭。
- 19 个测试文件中的每个 `TEST` 用例前都有中文测试意图。

本次没有新增接口或抽象层，也没有修改业务行为；现有配置、服务和 realtime 抽象均已接入主流程。

## 验证结果

执行：

```bash
clang-format -i <本次修改的 C++ 文件>
cmake --build build -j
ctest --test-dir build --output-on-failure
git diff --check
```

结果：

- 全量构建成功。
- 155/155 测试通过。
- diff 空白检查通过。
- 链接阶段仍会输出现有 macOS SDK、PoDoFo 静态库的 DWARF/weak-symbol 警告，但不影响构建和测试；本次没有修改依赖或链接配置。

## 下一步建议

后续新增代码继续沿用本次审计口径：先解释 public 契约和关键数据流，再解释不直观边界；不要为了提高注释比例重复描述简单 getter、赋值或循环。
