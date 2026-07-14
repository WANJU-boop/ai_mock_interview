#include "services/audio/pcm_sample_queue.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>

// 验证队列拒绝 0 容量，避免音频 callback 在没有可用槽位时出现取模除零或不清晰的丢帧行为。
TEST(PcmSampleQueueTest, RejectsZeroCapacity) {
    EXPECT_THROW(interview::services::PcmSampleQueue(0), std::invalid_argument);
}

// 验证完整 PCM 块按 FIFO 顺序流动。该性质保证语音采样不会在发送给 ASR 前被重排。
TEST(PcmSampleQueueTest, PreservesSampleOrderAcrossWrapAround) {
    interview::services::PcmSampleQueue queue(4);
    const std::array<std::int16_t, 3> first = {1, 2, 3};
    const std::array<std::int16_t, 2> second = {4, 5};
    std::array<std::int16_t, 2> partial = {};
    std::array<std::int16_t, 3> result = {};

    ASSERT_TRUE(queue.tryPush(first.data(), first.size()));
    ASSERT_TRUE(queue.tryPop(partial.data(), partial.size()));
    ASSERT_TRUE(queue.tryPush(second.data(), second.size()));
    ASSERT_TRUE(queue.tryPop(result.data(), result.size()));

    EXPECT_EQ(partial, (std::array<std::int16_t, 2>{1, 2}));
    EXPECT_EQ(result, (std::array<std::int16_t, 3>{3, 4, 5}));
}

// 验证空间不足时整块拒绝，而不是写入一半数据；半块会破坏多声道 frame 和上层时间轴。
TEST(PcmSampleQueueTest, RejectsWholeChunkWhenCapacityIsInsufficient) {
    interview::services::PcmSampleQueue queue(3);
    const std::array<std::int16_t, 3> full = {1, 2, 3};
    const std::array<std::int16_t, 1> overflow = {4};
    std::array<std::int16_t, 3> result = {};

    ASSERT_TRUE(queue.tryPush(full.data(), full.size()));
    EXPECT_FALSE(queue.tryPush(overflow.data(), overflow.size()));
    ASSERT_TRUE(queue.tryPop(result.data(), result.size()));
    EXPECT_EQ(result, full);
}

// 验证消费者只读取已经完整到齐的块。实时 worker 必须用 tryPop 返回值而不是预估数量做决定。
TEST(PcmSampleQueueTest, DoesNotPopPartialRequestedChunk) {
    interview::services::PcmSampleQueue queue(4);
    const std::array<std::int16_t, 2> source = {1, 2};
    std::array<std::int16_t, 3> destination = {};

    ASSERT_TRUE(queue.tryPush(source.data(), source.size()));
    EXPECT_FALSE(queue.tryPop(destination.data(), destination.size()));
    EXPECT_EQ(queue.availableSamples(), 2u);
}

// 验证播放端可消费不足一个设备 callback 的 TTS 尾包；剩余样本会由 PortAudio callback 补静音。
TEST(PcmSampleQueueTest, PopsAvailableTailWithoutDroppingIt) {
    interview::services::PcmSampleQueue queue(4);
    const std::array<std::int16_t, 2> source = {7, 8};
    std::array<std::int16_t, 4> destination = {};

    ASSERT_TRUE(queue.tryPush(source.data(), source.size()));
    EXPECT_EQ(queue.tryPopUpTo(destination.data(), destination.size()), 2u);
    EXPECT_EQ(destination[0], 7);
    EXPECT_EQ(destination[1], 8);
    EXPECT_EQ(queue.availableSamples(), 0u);
}
