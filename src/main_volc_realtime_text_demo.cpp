#include "services/realtime/volc/beast_volc_realtime_transport.h"
#include "services/realtime/volc/volc_realtime_client.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::string requireEnv(const char* name) {
    // 手动 demo 从环境变量读取密钥，避免把真实 App ID / Access Key 写进仓库。
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        throw std::runtime_error(std::string("请先设置环境变量：") + name);
    }

    return value;
}

std::string makeId(const std::string& prefix) {
    // 火山连接和会话都需要可追踪 ID。这里用时间戳生成简单 ID，
    // 只用于手动 smoke test，不要求跨机器全局唯一。
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return prefix + "-" + std::to_string(milliseconds);
}

std::string payloadToString(const interview::services::VolcRealtimeFrame& frame) {
    // 文本 demo 只打印 ChatResponse 的 JSON payload；不要打印鉴权 header 或完整请求配置。
    return {frame.payload.begin(), frame.payload.end()};
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        // 这个入口是手动集成检查，不属于默认单元测试。
        // 运行前需要在 shell 里设置 VOLC_APP_ID 和 VOLC_ACCESS_KEY。
        interview::services::VolcRealtimeClientConfig config;
        config.app_id = requireEnv("VOLC_APP_ID");
        config.access_key = requireEnv("VOLC_ACCESS_KEY");
        config.connect_id = makeId("connect");
        config.session_id = makeId("session");

        const std::string query = argc > 1 ? argv[1] : "请用一句中文介绍一下 RAII。";
        // 真实 transport 只在手动集成入口创建；单元测试仍注入 fake，避免默认测试联网。
        auto transport = std::make_shared<interview::services::BeastVolcRealtimeTransport>();
        interview::services::VolcRealtimeClient client(config, transport);

        // 火山文本模式的最小真实调用顺序：
        // WebSocket 握手 -> StartConnection -> StartSession(text) -> ChatTextQuery -> ChatEnded ->
        // Finish。
        client.connect();
        client.startConnection();
        client.receiveUntilEvent(interview::services::VolcRealtimeEventId::kConnectionStarted);
        client.startTextSession();
        client.receiveUntilEvent(interview::services::VolcRealtimeEventId::kSessionStarted);
        client.sendTextQuery(query);

        // 服务端可能先返回多帧中间结果，读取到 ChatEnded 才表示这一轮文本对话完整结束。
        const std::vector<interview::services::VolcRealtimeFrame> frames =
            client.receiveUntilChatEnded();
        for (const interview::services::VolcRealtimeFrame& frame : frames) {
            if (frame.event_id == interview::services::VolcRealtimeEventId::kChatResponse) {
                // 这里只打印服务端回答 payload，方便人工确认真实接口能返回文本。
                std::cout << payloadToString(frame) << '\n';
            }
        }

        // 按“会话 -> 业务连接 -> WebSocket”的顺序收口，避免直接断开留下半结束状态。
        client.finishSession();
        client.finishConnection();
        client.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "火山 realtime 文本 demo 失败：" << error.what() << '\n';
        return 1;
    }
}
