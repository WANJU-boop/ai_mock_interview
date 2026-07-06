#include "services/beast_volc_realtime_transport.h"
#include "services/volc_realtime_client.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::string requireEnv(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty()) {
        throw std::runtime_error(std::string("请先设置环境变量：") + name);
    }

    return value;
}

std::string makeId(const std::string& prefix) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return prefix + "-" + std::to_string(milliseconds);
}

std::string payloadToString(const interview::services::VolcRealtimeFrame& frame) {
    return {frame.payload.begin(), frame.payload.end()};
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        interview::services::VolcRealtimeClientConfig config;
        config.app_id = requireEnv("VOLC_APP_ID");
        config.access_key = requireEnv("VOLC_ACCESS_KEY");
        config.connect_id = makeId("connect");
        config.session_id = makeId("session");

        const std::string query = argc > 1 ? argv[1] : "请用一句中文介绍一下 RAII。";
        auto transport = std::make_shared<interview::services::BeastVolcRealtimeTransport>();
        interview::services::VolcRealtimeClient client(config, transport);

        client.connect();
        client.startConnection();
        client.receiveUntilEvent(interview::services::VolcRealtimeEventId::kConnectionStarted);
        client.startTextSession();
        client.receiveUntilEvent(interview::services::VolcRealtimeEventId::kSessionStarted);
        client.sendTextQuery(query);

        const std::vector<interview::services::VolcRealtimeFrame> frames =
            client.receiveUntilChatEnded();
        for (const interview::services::VolcRealtimeFrame& frame : frames) {
            if (frame.event_id == interview::services::VolcRealtimeEventId::kChatResponse) {
                std::cout << payloadToString(frame) << '\n';
            }
        }

        client.finishSession();
        client.finishConnection();
        client.close();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "火山 realtime 文本 demo 失败：" << error.what() << '\n';
        return 1;
    }
}
