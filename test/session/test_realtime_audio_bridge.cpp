#include "services/audio/mock/fake_audio_device.h"
#include "services/realtime/mock/mock_realtime_client.h"
#include "session/realtime_audio_bridge.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace {

interview::services::AudioPcmFormat makeFormat() {
    return {16000, 1, 320};
}

interview::services::AudioPcmChunk makeChunk(std::initializer_list<std::int16_t> samples) {
    return {std::vector<std::int16_t>(samples)};
}

} // namespace

// 验证 bridge 在同一 worker 内把设备采集块转交给 realtime client，不需要真实麦克风或 WebSocket。
TEST(RealtimeAudioBridgeTest, PumpsCapturedAudioIntoRealtimeClient) {
    interview::services::FakeAudioDevice audio_device({makeChunk({1, 2, 3, 4})});
    interview::services::MockRealtimeClient realtime_client({});
    ASSERT_TRUE(realtime_client.connect());
    interview::session::RealtimeAudioBridge bridge(audio_device, realtime_client, makeFormat(),
                                                   makeFormat());

    ASSERT_TRUE(bridge.start());
    interview::common::RealtimeEvent tts_ended;
    tts_ended.type = interview::common::RealtimeEventType::kTtsEnded;
    ASSERT_TRUE(bridge.consumeRealtimeEvent(tts_ended));
    ASSERT_TRUE(bridge.pumpCapturedAudio());

    ASSERT_EQ(realtime_client.getOutboundCandidateAudioChunks().size(), 1u);
    EXPECT_EQ(realtime_client.getOutboundCandidateAudioChunks().front().samples,
              std::vector<std::int16_t>({1, 2, 3, 4}));
}

// 验证一次 pump 只发送一块录音，防止真实麦克风持续生产时 worker 永远困在上传循环。
// 后续调用仍能继续发送剩余块，说明“让出执行权”不会丢录音数据。
TEST(RealtimeAudioBridgeTest, BoundsCaptureWorkPerPumpWithoutDroppingQueuedChunks) {
    interview::services::FakeAudioDevice audio_device(
        {makeChunk({1}), makeChunk({2}), makeChunk({3}), makeChunk({4}), makeChunk({5})});
    interview::services::MockRealtimeClient realtime_client({});
    ASSERT_TRUE(realtime_client.connect());
    interview::session::RealtimeAudioBridge bridge(audio_device, realtime_client, makeFormat(),
                                                   makeFormat());

    ASSERT_TRUE(bridge.start());
    interview::common::RealtimeEvent tts_ended;
    tts_ended.type = interview::common::RealtimeEventType::kTtsEnded;
    ASSERT_TRUE(bridge.consumeRealtimeEvent(tts_ended));

    ASSERT_TRUE(bridge.pumpCapturedAudio());
    EXPECT_EQ(realtime_client.getOutboundCandidateAudioChunks().size(), 1u);
    for (int index = 0; index < 4; ++index) {
        ASSERT_TRUE(bridge.pumpCapturedAudio());
    }
    ASSERT_EQ(realtime_client.getOutboundCandidateAudioChunks().size(), 5u);
    EXPECT_EQ(realtime_client.getOutboundCandidateAudioChunks().back().samples,
              std::vector<std::int16_t>({5}));
}

// 验证底层播放状态长期不归零时，bridge 仍会按已知 PCM 时长截止点恢复真实麦克风。
// 测试不放入 TTS 样本，因此截止点在过去，可立即覆盖兜底分支而无需真实等待。
TEST(RealtimeAudioBridgeTest, ResumesCaptureWhenPlaybackPendingStateIsStale) {
    interview::services::FakeAudioDevice audio_device({makeChunk({7, -8})});
    audio_device.setPlaybackPendingForTesting(true);
    interview::services::MockRealtimeClient realtime_client({});
    ASSERT_TRUE(realtime_client.connect());
    interview::session::RealtimeAudioBridge bridge(audio_device, realtime_client, makeFormat(),
                                                   makeFormat());

    ASSERT_TRUE(bridge.start());
    interview::common::RealtimeEvent tts_ended;
    tts_ended.type = interview::common::RealtimeEventType::kTtsEnded;
    ASSERT_TRUE(bridge.consumeRealtimeEvent(tts_ended));
    ASSERT_TRUE(bridge.pumpCapturedAudio());

    ASSERT_EQ(realtime_client.getOutboundCandidateAudioChunks().size(), 1u);
    EXPECT_EQ(realtime_client.getOutboundCandidateAudioChunks().front().samples,
              std::vector<std::int16_t>({7, -8}));
}

// 验证面试官 TTS 期间仍按 keep_alive 节奏发送音频包，但把麦克风内容替换为静音。
// 这样扬声器的问题不会被 ASR 再识别成候选人回答。
TEST(RealtimeAudioBridgeTest, SendsSilenceWhileInterviewerAudioIsActive) {
    interview::services::FakeAudioDevice audio_device({makeChunk({1, -2, 3, -4})});
    interview::services::MockRealtimeClient realtime_client({});
    ASSERT_TRUE(realtime_client.connect());
    interview::session::RealtimeAudioBridge bridge(audio_device, realtime_client, makeFormat(),
                                                   makeFormat());

    ASSERT_TRUE(bridge.start());
    bridge.suspendCaptureForwarding();
    ASSERT_TRUE(bridge.pumpCapturedAudio());

    ASSERT_EQ(realtime_client.getOutboundCandidateAudioChunks().size(), 1u);
    EXPECT_EQ(realtime_client.getOutboundCandidateAudioChunks().front().samples,
              std::vector<std::int16_t>({0, 0, 0, 0}));
}

// 验证 TTS 的小端 PCM payload 会被解码为 int16 样本并送到播放队列，防止 CPU 字节序影响声音内容。
TEST(RealtimeAudioBridgeTest, DecodesLittleEndianTtsPayloadForPlayback) {
    interview::services::FakeAudioDevice audio_device({});
    interview::services::MockRealtimeClient realtime_client({});
    ASSERT_TRUE(realtime_client.connect());
    interview::session::RealtimeAudioBridge bridge(audio_device, realtime_client, makeFormat(),
                                                   makeFormat());
    ASSERT_TRUE(bridge.start());

    interview::common::RealtimeEvent tts_event;
    tts_event.type = interview::common::RealtimeEventType::kInterviewerText;
    // 0x0001, 0xFFFE 分别验证正常值和负数的 two's-complement 小端解码。
    tts_event.payload = {0x01, 0x00, 0xFE, 0xFF};

    ASSERT_TRUE(bridge.consumeRealtimeEvent(tts_event));
    ASSERT_EQ(audio_device.getPlayedChunks().size(), 1u);
    EXPECT_EQ(audio_device.getPlayedChunks().front().samples, std::vector<std::int16_t>({1, -2}));
}

// 验证字节数为奇数的 TTS 数据被拒绝。s16le 在半个采样处截断时不能尝试播放或静默补零。
TEST(RealtimeAudioBridgeTest, RejectsOddSizedTtsPayload) {
    interview::services::FakeAudioDevice audio_device({});
    interview::services::MockRealtimeClient realtime_client({});
    ASSERT_TRUE(realtime_client.connect());
    interview::session::RealtimeAudioBridge bridge(audio_device, realtime_client, makeFormat(),
                                                   makeFormat());
    ASSERT_TRUE(bridge.start());

    interview::common::RealtimeEvent tts_event;
    tts_event.type = interview::common::RealtimeEventType::kInterviewerText;
    tts_event.payload = {0x01, 0x00, 0xFF};

    EXPECT_FALSE(bridge.consumeRealtimeEvent(tts_event));
    EXPECT_TRUE(audio_device.getPlayedChunks().empty());
}

// 验证停止顺序可重复执行，确保会话正常结束和错误结束都不会留下 fake 音频资源。
TEST(RealtimeAudioBridgeTest, StopsAudioDeviceIdempotently) {
    interview::services::FakeAudioDevice audio_device({});
    interview::services::MockRealtimeClient realtime_client({});
    ASSERT_TRUE(realtime_client.connect());
    interview::session::RealtimeAudioBridge bridge(audio_device, realtime_client, makeFormat(),
                                                   makeFormat());
    ASSERT_TRUE(bridge.start());

    bridge.stop();
    bridge.stop();

    EXPECT_TRUE(audio_device.isStopped());
}
