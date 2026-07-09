#pragma once

#include "common/realtime_protocol.h"

#include <string>

namespace interview {
namespace services {

// Realtime 客户端接口隔离语音服务能力，让 session 层只处理业务事件，不直接依赖 WebSocket。
class IRealtimeClient {
  public:
    virtual ~IRealtimeClient() = default;

    // 建立 realtime 会话。mock 只切换内部状态，真实实现后续再建立 WSS 连接。
    virtual bool connect() = 0;

    // 判断是否还有可消费的服务端事件；真实实现可以映射为阻塞读取或事件队列。
    virtual bool hasNextEvent() const = 0;

    // 取出下一条业务事件。调用方应先检查 hasNextEvent，避免空队列读取。
    virtual common::RealtimeEvent receiveNextEvent() = 0;

    // 发送面试官文本；后续真实实现会把它转成 TTS 或服务端 response 指令。
    virtual bool sendInterviewerText(const std::string& text) = 0;

    // 主动关闭 realtime 会话，确保后续 WebSocket 实现有明确的停止入口。
    virtual void close() = 0;
};

} // namespace services
} // namespace interview
