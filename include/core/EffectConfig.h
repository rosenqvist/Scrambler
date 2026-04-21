#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <unordered_set>

namespace scrambler::core
{

inline constexpr int kDefaultDuplicateCopies = 1;
inline constexpr int kMaxDuplicateCopies = 64;

struct DirectionEffectSnapshot
{
    std::chrono::milliseconds delay{0};
    float drop_rate = 0.0F;
    float duplicate_rate = 0.0F;
    int duplicate_count = kDefaultDuplicateCopies;
};

class DirectionEffectConfig
{
public:
    [[nodiscard]] DirectionEffectSnapshot Snapshot() const
    {
        return {.delay = std::chrono::milliseconds(DelayMs()),
                .drop_rate = DropRate(),
                .duplicate_rate = DuplicateRate(),
                .duplicate_count = DuplicateCount()};
    }

    [[nodiscard]] int DelayMs() const
    {
        return delay_ms_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::chrono::milliseconds Delay() const
    {
        return std::chrono::milliseconds(DelayMs());
    }

    void SetDelayMs(int delay_ms)
    {
        delay_ms_.store((std::max) (delay_ms, 0), std::memory_order_relaxed);
    }

    [[nodiscard]] float DropRate() const
    {
        return drop_rate_.load(std::memory_order_relaxed);
    }

    void SetDropRate(float drop_rate)
    {
        drop_rate_.store(std::clamp(drop_rate, 0.0F, 1.0F), std::memory_order_relaxed);
    }

    [[nodiscard]] float DuplicateRate() const
    {
        return duplicate_rate_.load(std::memory_order_relaxed);
    }

    void SetDuplicateRate(float duplicate_rate)
    {
        duplicate_rate_.store(std::clamp(duplicate_rate, 0.0F, 1.0F), std::memory_order_relaxed);
    }

    [[nodiscard]] int DuplicateCount() const
    {
        return duplicate_count_.load(std::memory_order_relaxed);
    }

    void SetDuplicateCount(int duplicate_count)
    {
        duplicate_count_.store(std::clamp(duplicate_count, kDefaultDuplicateCopies, kMaxDuplicateCopies),
                               std::memory_order_relaxed);
    }

private:
    std::atomic<int> delay_ms_{0};
    std::atomic<float> drop_rate_{0.0F};
    std::atomic<float> duplicate_rate_{0.0F};
    std::atomic<int> duplicate_count_{kDefaultDuplicateCopies};
};

struct EffectConfig
{
public:
    DirectionEffectConfig outbound;
    DirectionEffectConfig inbound;

    [[nodiscard]] DirectionEffectSnapshot Snapshot(bool is_outbound) const
    {
        return Direction(is_outbound).Snapshot();
    }

    [[nodiscard]] std::chrono::milliseconds Delay(bool is_outbound) const
    {
        return Direction(is_outbound).Delay();
    }

    [[nodiscard]] float DropRate(bool is_outbound) const
    {
        return Direction(is_outbound).DropRate();
    }

    [[nodiscard]] float DuplicateRate(bool is_outbound) const
    {
        return Direction(is_outbound).DuplicateRate();
    }

    [[nodiscard]] int DuplicateCount(bool is_outbound) const
    {
        return Direction(is_outbound).DuplicateCount();
    }

    void SetDelayMs(bool is_outbound, int delay_ms)
    {
        Direction(is_outbound).SetDelayMs(delay_ms);
    }

    void SetDropRate(bool is_outbound, float drop_rate)
    {
        Direction(is_outbound).SetDropRate(drop_rate);
    }

    void SetDuplicateRate(bool is_outbound, float duplicate_rate)
    {
        Direction(is_outbound).SetDuplicateRate(duplicate_rate);
    }

    void SetDuplicateCount(bool is_outbound, int duplicate_count)
    {
        Direction(is_outbound).SetDuplicateCount(duplicate_count);
    }

    [[nodiscard]] DirectionEffectConfig& Direction(bool is_outbound)
    {
        return is_outbound ? outbound : inbound;
    }

    [[nodiscard]] const DirectionEffectConfig& Direction(bool is_outbound) const
    {
        return is_outbound ? outbound : inbound;
    }
};

inline bool ShouldApplyRate(float rate, std::mt19937& rng)
{
    if (rate <= 0.0F)
    {
        return false;
    }

    if (rate >= 1.0F)
    {
        return true;
    }

    return std::uniform_real_distribution<float>(0.0F, 1.0F)(rng) < rate;
}

inline bool ShouldDrop(float rate, std::mt19937& rng)
{
    return ShouldApplyRate(rate, rng);
}

inline bool ShouldDrop(float rate)
{
    thread_local std::mt19937 rng{std::random_device{}()};
    return ShouldApplyRate(rate, rng);
}

class TargetSet
{
public:
    void Add(uint32_t pid)
    {
        std::lock_guard lock(mutex_);
        pids_.insert(pid);
    }

    void Remove(uint32_t pid)
    {
        std::lock_guard lock(mutex_);
        pids_.erase(pid);
    }

    void Clear()
    {
        std::lock_guard lock(mutex_);
        pids_.clear();
    }

    void SetSingle(uint32_t pid)
    {
        std::lock_guard lock(mutex_);
        pids_.clear();
        pids_.insert(pid);
    }

    bool Contains(uint32_t pid) const
    {
        std::lock_guard lock(mutex_);
        return pids_.contains(pid);
    }

private:
    mutable std::mutex mutex_;
    std::unordered_set<uint32_t> pids_;
};

}  // namespace scrambler::core
