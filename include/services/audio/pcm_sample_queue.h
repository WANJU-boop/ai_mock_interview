#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace interview {
namespace services {

// 单生产者、单消费者（SPSC）的固定容量 PCM 环形队列。
// PortAudio 回调是唯一生产者/消费者，realtime worker 是另一端；两侧都不加锁、
// 不分配内存，也不调用网络，从而避免音频实时线程因锁竞争或 I/O 出现爆音。
class PcmSampleQueue final {
  public:
    // capacity_samples 是可存放的实际采样值数量，而不是字节数或帧数。
    explicit PcmSampleQueue(std::size_t capacity_samples) : capacity_samples_(capacity_samples) {
        // 环形队列额外保留一个哨兵槽位区分“空”和“满”；0 容量没有可定义的队列语义。
        if (capacity_samples == 0 || capacity_samples == static_cast<std::size_t>(-1)) {
            throw std::invalid_argument("PCM 队列容量必须为正数。");
        }
        samples_.resize(capacity_samples + 1);
    }

    // 生产者一次写入完整块。空间不足时返回 false 且不写入部分数据，
    // 让调用方可以明确计数或丢弃整块，而不会让多声道 PCM 在块中间断裂。
    bool tryPush(const std::int16_t* source, std::size_t count) {
        if (source == nullptr || count == 0 || count > capacity_samples_) {
            return false;
        }

        const std::size_t read_index = read_index_.load(std::memory_order_acquire);
        const std::size_t write_index = write_index_.load(std::memory_order_relaxed);
        if (count > capacity_samples_ - distance(read_index, write_index)) {
            return false;
        }

        for (std::size_t index = 0; index < count; ++index) {
            samples_[(write_index + index) % samples_.size()] = source[index];
        }
        // 先写样本，再 release 发布新的写位置；消费者 acquire 后才能看到完整块。
        write_index_.store((write_index + count) % samples_.size(), std::memory_order_release);
        return true;
    }

    // 消费者只在完整块已经到齐时读取。PCM 以块发送/播放，禁止读取半块避免帧边界漂移。
    bool tryPop(std::int16_t* destination, std::size_t count) {
        if (destination == nullptr || count == 0 || count > capacity_samples_) {
            return false;
        }

        const std::size_t read_index = read_index_.load(std::memory_order_relaxed);
        const std::size_t write_index = write_index_.load(std::memory_order_acquire);
        if (distance(read_index, write_index) < count) {
            return false;
        }

        for (std::size_t index = 0; index < count; ++index) {
            destination[index] = samples_[(read_index + index) % samples_.size()];
        }
        // 先复制给消费者，再 release 空出的空间；生产者之后才能安全复用这些槽位。
        read_index_.store((read_index + count) % samples_.size(), std::memory_order_release);
        return true;
    }

    // 音频播放 callback 可接受不足一帧缓冲的 TTS 尾包：先取当前可用样本，剩余部分由 callback
    // 补静音。 返回 0 表示暂时无数据；它不会像 tryPop 那样要求完整的 requested block。
    std::size_t tryPopUpTo(std::int16_t* destination, std::size_t maximum_count) {
        if (destination == nullptr || maximum_count == 0) {
            return 0;
        }

        const std::size_t read_index = read_index_.load(std::memory_order_relaxed);
        const std::size_t write_index = write_index_.load(std::memory_order_acquire);
        const std::size_t count = std::min(maximum_count, distance(read_index, write_index));
        for (std::size_t index = 0; index < count; ++index) {
            destination[index] = samples_[(read_index + index) % samples_.size()];
        }
        if (count > 0) {
            read_index_.store((read_index + count) % samples_.size(), std::memory_order_release);
        }
        return count;
    }

    // 该查询仅用于 worker 决定是否尝试读取，不用于“先检查再读”的并发正确性保证。
    // 调用方仍必须以 tryPop 的返回值作为唯一的成功依据。
    std::size_t availableSamples() const {
        const std::size_t read_index = read_index_.load(std::memory_order_relaxed);
        const std::size_t write_index = write_index_.load(std::memory_order_acquire);
        return distance(read_index, write_index);
    }

  private:
    std::size_t distance(std::size_t from, std::size_t to) const {
        return (to + samples_.size() - from) % samples_.size();
    }

    std::vector<std::int16_t> samples_;
    std::size_t capacity_samples_;
    // 写索引只能由生产者修改，读索引只能由消费者修改，符合 SPSC 的所有权约束。
    std::atomic<std::size_t> write_index_{0};
    std::atomic<std::size_t> read_index_{0};
};

} // namespace services
} // namespace interview
