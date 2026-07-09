#include "services/realtime/volc/volc_realtime_client.h"

#include <deque>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeVolcRealtimeTransport final : public interview::services::IVolcRealtimeTransport {
  public:
    void connect(const interview::services::VolcRealtimeConnectionRequest& request) override {
        // fake 不联网，只记录握手请求，测试就能检查 URL、header、timeout 是否正确。
        connected = true;
        last_request = request;
    }

    void sendBinary(const std::vector<std::uint8_t>& bytes) override {
        // 模拟真实 transport 的基本状态约束：未连接或已关闭时不能发送。
        // 这能防止测试误以为 client 可以跳过 connect 直接发包。
        if (!connected || closed) {
            throw std::runtime_error("fake transport is not connected");
        }
        sent_frames.push_back(bytes);
    }

    std::vector<std::uint8_t> receiveBinary() override {
        // incoming_frames 模拟服务端按顺序返回的 WebSocket binary message。
        // 如果脚本耗尽还继续读，说明被测逻辑没有正确等到目标事件或缺少边界处理。
        if (incoming_frames.empty()) {
            throw std::out_of_range("no incoming frame");
        }

        std::vector<std::uint8_t> frame = incoming_frames.front();
        incoming_frames.pop_front();
        return frame;
    }

    void close() override {
        closed = true;
    }

    bool connected = false;
    bool closed = false;
    // 最近一次 connect 请求，专门用于验证鉴权 header 是否由 client 正确构造。
    interview::services::VolcRealtimeConnectionRequest last_request;
    // client 发出的每个二进制 frame 都保存在这里，测试再用协议层 decode 回来检查 event id/payload。
    std::vector<std::vector<std::uint8_t>> sent_frames;
    // 预置服务端返回脚本，测试可以精确控制 ChatResponse、ChatEnded 等事件顺序。
    std::deque<std::vector<std::uint8_t>> incoming_frames;
};

interview::services::VolcRealtimeRuntimeConfig makeConfig() {
    // 使用假的 app/access key，保证单元测试不需要真实账号，也不会把密钥写进仓库。
    // 运行时配置没有供应商默认值，测试必须显式写全字段，避免遗漏映射却被默认值掩盖。
    interview::services::VolcRealtimeRuntimeConfig config;
    config.endpoint = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue";
    config.app_id = "test-app-id";
    config.access_key = "test-access-key";
    config.resource_id = "volc.speech.dialog";
    config.app_key = "PlgvMymc7f3tQnJ6";
    config.connect_id = "connect-001";
    config.session_id = "session-001";
    config.model = "1.2.1.1";
    config.input_mod = "text";
    config.strict_audit = true;
    config.enable_volc_websearch = false;
    config.speaker = "zh_female_vv_jupiter_bigtts";
    config.tts_audio_format = "pcm_s16le";
    config.tts_sample_rate_hz = 24000;
    config.tts_channels = 1;
    config.timeout_ms = 12000;
    return config;
}

std::string findHeaderValue(const std::vector<interview::services::VolcRealtimeHeader>& headers,
                            const std::string& name) {
    // header 顺序不是业务重点，测试按名字查找，比依赖 vector 下标更稳定。
    for (const interview::services::VolcRealtimeHeader& header : headers) {
        if (header.name == name) {
            return header.value;
        }
    }

    return "";
}

std::vector<std::uint8_t> toBytes(const std::string& text) {
    // 服务端 JSON fixture 按字节放入 payload，和真实 WebSocket binary message 更接近。
    return {text.begin(), text.end()};
}

std::vector<std::uint8_t> makeServerEvent(interview::services::VolcRealtimeEventId event_id,
                                          const std::string& payload) {
    // 构造“服务端发来的火山事件 frame”。测试 client 收包逻辑时，
    // 不应该直接塞 VolcRealtimeFrame，而应该塞编码后的 bytes，覆盖真实解码路径。
    interview::services::VolcRealtimeFrame frame;
    frame.message_type = interview::services::VolcRealtimeMessageType::kFullServerResponse;
    frame.flag = interview::services::VolcRealtimeMessageFlag::kEvent;
    frame.serialization = interview::services::VolcRealtimeSerialization::kJson;
    frame.event_id = event_id;
    frame.session_id = "session-001";
    frame.payload = toBytes(payload);
    return interview::services::encodeVolcRealtimeFrame(frame);
}

} // namespace

// 验证 WebSocket 握手 header 会完整写入 fake transport，真实密钥只在内存中传递，不写入配置文件。
TEST(VolcRealtimeClientTest, ConnectBuildsRequiredWebSocketHeaders) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    interview::services::VolcRealtimeClient client(makeConfig(), transport);

    client.connect();

    EXPECT_TRUE(transport->connected);
    EXPECT_EQ(transport->last_request.url,
              "wss://openspeech.bytedance.com/api/v3/realtime/dialogue");
    EXPECT_EQ(transport->last_request.timeout_ms, 12000);
    EXPECT_EQ(findHeaderValue(transport->last_request.headers, "X-Api-App-ID"), "test-app-id");
    EXPECT_EQ(findHeaderValue(transport->last_request.headers, "X-Api-Access-Key"),
              "test-access-key");
    EXPECT_EQ(findHeaderValue(transport->last_request.headers, "X-Api-Resource-Id"),
              "volc.speech.dialog");
    EXPECT_EQ(findHeaderValue(transport->last_request.headers, "X-Api-App-Key"),
              "PlgvMymc7f3tQnJ6");
    EXPECT_EQ(findHeaderValue(transport->last_request.headers, "X-Api-Connect-Id"), "connect-001");
}

// 验证 StartConnection 和 StartSession 会按火山事件 ID 顺序发送，StartSession 固定使用 text 模式。
TEST(VolcRealtimeClientTest, SendsStartConnectionAndTextSessionEvents) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    interview::services::VolcRealtimeClient client(makeConfig(), transport);

    client.connect();
    client.startConnection();
    client.startTextSession();

    ASSERT_EQ(transport->sent_frames.size(), 2u);
    const interview::services::VolcRealtimeFrame start_connection =
        interview::services::decodeVolcRealtimeFrame(transport->sent_frames[0]);
    const interview::services::VolcRealtimeFrame start_session =
        interview::services::decodeVolcRealtimeFrame(transport->sent_frames[1]);

    EXPECT_EQ(start_connection.event_id,
              interview::services::VolcRealtimeEventId::kStartConnection);
    EXPECT_TRUE(start_connection.session_id.empty());
    EXPECT_EQ(start_session.event_id, interview::services::VolcRealtimeEventId::kStartSession);
    EXPECT_EQ(start_session.session_id, "session-001");

    const std::string payload(start_session.payload.begin(), start_session.payload.end());
    EXPECT_NE(payload.find(R"("input_mod":"text")"), std::string::npos);
    EXPECT_NE(payload.find(R"("model":"1.2.1.1")"), std::string::npos);
    EXPECT_NE(payload.find(R"("format":"pcm_s16le")"), std::string::npos);
    EXPECT_NE(payload.find(R"("sample_rate":24000)"), std::string::npos);
    EXPECT_NE(payload.find(R"("strict_audit":true)"), std::string::npos);
}

// 验证文本 query 会发送 ChatTextQuery，并读取到 ChatEnded 为止，形成文本模式的最小收发闭环。
TEST(VolcRealtimeClientTest, SendsTextQueryAndReceivesUntilChatEnded) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    transport->incoming_frames.push_back(
        // 第一帧模拟服务端生成的文本回答。
        makeServerEvent(interview::services::VolcRealtimeEventId::kChatResponse,
                        R"({"content":"RAII 是资源获取即初始化。"})"));
    transport->incoming_frames.push_back(
        // 第二帧模拟本轮 Chat 完成；receiveUntilChatEnded 应该在这里停止。
        makeServerEvent(interview::services::VolcRealtimeEventId::kChatEnded,
                        R"({"question_id":"q1","reply_id":"r1"})"));
    interview::services::VolcRealtimeClient client(makeConfig(), transport);

    client.connect();
    client.sendTextQuery("请解释 RAII。");
    const std::vector<interview::services::VolcRealtimeFrame> frames =
        client.receiveUntilChatEnded();

    ASSERT_EQ(transport->sent_frames.size(), 1u);
    const interview::services::VolcRealtimeFrame query =
        interview::services::decodeVolcRealtimeFrame(transport->sent_frames[0]);
    EXPECT_EQ(query.event_id, interview::services::VolcRealtimeEventId::kChatTextQuery);
    EXPECT_EQ(query.session_id, "session-001");
    EXPECT_NE(std::string(query.payload.begin(), query.payload.end()).find("请解释 RAII。"),
              std::string::npos);

    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].event_id, interview::services::VolcRealtimeEventId::kChatResponse);
    EXPECT_EQ(frames[1].event_id, interview::services::VolcRealtimeEventId::kChatEnded);
}

// 验证结束阶段会先发送 FinishSession，再发送 FinishConnection，最后关闭底层 transport。
TEST(VolcRealtimeClientTest, SendsFinishEventsAndClosesTransport) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    interview::services::VolcRealtimeClient client(makeConfig(), transport);

    client.connect();
    client.finishSession();
    client.finishConnection();
    client.close();

    ASSERT_EQ(transport->sent_frames.size(), 2u);
    EXPECT_EQ(interview::services::decodeVolcRealtimeFrame(transport->sent_frames[0]).event_id,
              interview::services::VolcRealtimeEventId::kFinishSession);
    EXPECT_EQ(interview::services::decodeVolcRealtimeFrame(transport->sent_frames[1]).event_id,
              interview::services::VolcRealtimeEventId::kFinishConnection);
    EXPECT_TRUE(transport->closed);
}

// 验证配置缺少 access key 时构造阶段直接失败，避免真实连接阶段才暴露模糊鉴权错误。
TEST(VolcRealtimeClientTest, ThrowsWhenRequiredConfigIsMissing) {
    interview::services::VolcRealtimeRuntimeConfig config = makeConfig();
    config.access_key.clear();

    EXPECT_THROW(interview::services::VolcRealtimeClient(
                     config, std::make_shared<FakeVolcRealtimeTransport>()),
                 std::runtime_error);
}
