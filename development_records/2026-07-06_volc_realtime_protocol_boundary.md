# 火山 Realtime 协议边界

## 本次目标

新增火山端到端实时语音大模型的二进制 frame 编解码边界，只处理供应商协议本身，不连接 WebSocket，也不接入面试业务流程。

## 核心数据流

```text
VolcRealtimeFrame
  -> encodeVolcRealtimeFrame
  -> 火山二进制 header / optional / payload size / payload
  -> decodeVolcRealtimeFrame
  -> VolcRealtimeFrame
```

该协议放在 `services` 层，因为它是火山私有协议，不是项目内部通用 realtime 事件格式。项目内部仍然保留 `common::RealtimeEvent` 作为 session、mock、UI 之间的稳定边界。

## 关键 C++ 知识点

- 使用 `enum class` 表达供应商 message type、flag、serialization、event id，避免裸整数在业务代码里扩散。
- 使用 `std::optional` 表达 code、sequence、event id 这类按 frame 类型才存在的字段。
- 使用大端序写入 32 位整数，匹配火山文档示例中的事件 ID 和 payload 长度布局。
- 对未知事件、短 header、payload 长度越界做 `std::invalid_argument` 防御，避免真实网络半包进入状态机。

## 验证结果

已运行：

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

结果：`136/136` 通过。

## 下一步建议

在协议边界稳定后，继续实现火山文本模式客户端，用 fake transport 离线验证握手 header、StartConnection、StartSession、ChatTextQuery 和 ChatEnded 收包流程。
