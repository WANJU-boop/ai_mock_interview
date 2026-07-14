#include "services/audio/mock/fake_audio_device.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace {

interview::services::AudioPcmFormat makeMonoFormat() {
    return {16000, 1, 320};
}

interview::services::AudioPcmChunk makeChunk(std::initializer_list<std::int16_t> samples) {
    return {std::vector<std::int16_t>(samples)};
}

} // namespace

// 验证 Fake 可以通过抽象接口使用。这个边界保证后续 PortAudio 替换不会迫使 session 层认识硬件 API。
TEST(FakeAudioDeviceTest, CanUseFakeThroughAudioDeviceInterface) {
    interview::services::FakeAudioDevice fake_device({makeChunk({1, 2, 3})});
    interview::services::IAudioDevice& device = fake_device;

    ASSERT_TRUE(device.startCapture(makeMonoFormat()));
    const std::optional<interview::services::AudioPcmChunk> chunk = device.tryReadCapturedChunk();

    ASSERT_TRUE(chunk.has_value());
    EXPECT_EQ(chunk->samples, std::vector<std::int16_t>({1, 2, 3}));
}

// 验证录音块严格按脚本顺序消费，并且耗尽时返回空值而不是阻塞测试线程。
TEST(FakeAudioDeviceTest, ReadsConfiguredCaptureChunksInOrder) {
    interview::services::FakeAudioDevice device({makeChunk({1, 2}), makeChunk({3, 4})});

    ASSERT_TRUE(device.startCapture(makeMonoFormat()));
    ASSERT_TRUE(device.tryReadCapturedChunk().has_value());
    EXPECT_EQ(device.tryReadCapturedChunk()->samples, std::vector<std::int16_t>({3, 4}));
    EXPECT_FALSE(device.tryReadCapturedChunk().has_value());
}

// 验证播放前必须显式启动输出设备，避免在真实环境中把 TTS 数据丢给未初始化的硬件流。
TEST(FakeAudioDeviceTest, RequiresPlaybackToStartBeforeAcceptingPcm) {
    interview::services::FakeAudioDevice device({});

    EXPECT_FALSE(device.playPcmChunk(makeChunk({1, 2, 3})));
    ASSERT_TRUE(device.startPlayback(makeMonoFormat()));
    EXPECT_TRUE(device.playPcmChunk(makeChunk({1, 2, 3})));
    ASSERT_EQ(device.getPlayedChunks().size(), 1u);
    EXPECT_EQ(device.getPlayedChunks().front().samples, std::vector<std::int16_t>({1, 2, 3}));
}

// 验证多声道 PCM 不能携带半帧。该边界在音频 API 前拒绝坏数据，比让播放或网络层猜测更安全。
TEST(FakeAudioDeviceTest, RejectsUnalignedPlaybackChunk) {
    interview::services::FakeAudioDevice device({});
    const interview::services::AudioPcmFormat stereo_format = {16000, 2, 320};

    ASSERT_TRUE(device.startPlayback(stereo_format));
    EXPECT_FALSE(device.playPcmChunk(makeChunk({1, 2, 3})));
    EXPECT_TRUE(device.getPlayedChunks().empty());
}

// 验证 stop 可以重复调用，且停止后不再允许采集或播放。这是错误、取消和析构路径共用清理入口的基础。
TEST(FakeAudioDeviceTest, StopsIdempotentlyAndRejectsFurtherIo) {
    interview::services::FakeAudioDevice device({makeChunk({1, 2, 3})});

    ASSERT_TRUE(device.startCapture(makeMonoFormat()));
    ASSERT_TRUE(device.startPlayback(makeMonoFormat()));
    device.stop();
    device.stop();

    EXPECT_TRUE(device.isStopped());
    EXPECT_FALSE(device.isCaptureStarted());
    EXPECT_FALSE(device.isPlaybackStarted());
    EXPECT_FALSE(device.tryReadCapturedChunk().has_value());
    EXPECT_FALSE(device.playPcmChunk(makeChunk({1, 2, 3})));
}
