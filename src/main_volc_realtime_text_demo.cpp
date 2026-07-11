#include "common/config.h"
#include "services/realtime/volc/beast_volc_realtime_transport.h"
#include "services/realtime/volc/volc_realtime_client.h"
#include "services/realtime/volc/volc_realtime_runtime.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace {

std::string payloadToString(const interview::services::VolcRealtimeFrame& frame) {
    // 文本 demo 只打印 ChatResponse 的 JSON payload；不要打印鉴权 header 或完整请求配置。
    return {frame.payload.begin(), frame.payload.end()};
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        // 这个入口是手动集成检查，不属于默认单元测试。
        // 第一个参数是统一配置文件，第二个参数才是可选问题；真实密钥仍由配置指定的环境变量注入。
        const std::string config_path =
            argc > 1 ? argv[1] : interview::common::findDefaultConfigPath(argv[0]);
        const interview::common::AppConfig app_config =
            interview::common::loadConfigFromFile(config_path);
        const interview::services::VolcRealtimeRuntimeConfig runtime_config =
            interview::services::resolveVolcRealtimeRuntimeConfig(app_config.realtime);
        const std::string query = argc > 2 ? argv[2] : "请用一句中文介绍一下 RAII。";

        // 真实 transport 只在手动集成入口创建；单元测试仍注入 fake，避免默认测试联网。
        auto transport = std::make_shared<interview::services::BeastVolcRealtimeTransport>();
        interview::services::VolcRealtimeClient client(runtime_config, transport);

        // 火山文本模式的最小真实调用顺序：
        // WebSocket 握手 -> StartConnection -> StartSession(text) -> ChatTextQuery -> ChatEnded ->
        // Finish。
        client.connect();
        client.startConnection();
        client.receiveUntilEvent(interview::services::VolcRealtimeEventId::kConnectionStarted);
        client.startSession();
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
