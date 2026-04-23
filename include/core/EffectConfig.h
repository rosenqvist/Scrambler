#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <random>
#include <unordered_set>

namespace scrambler::core
{

inline constexpr int kDefaultDuplicateCopies = 1;
inline constexpr int kMaxDuplicateCopies = 64;
inline constexpr int kMaxDelayJitterMs = 1000;
inline constexpr int kDefaultBurstDropLength = 3;
inline constexpr int kMaxBurstDropLength = 32;
inline constexpr int kMaxThrottleKBytesPerSec = 10240;

struct DirectionEffectSnapshot
{
    std::chrono::milliseconds delay{0};
    int delay_jitter_ms = 0;
    std::uint64_t throttle_bytes_per_second = 0;
    std::uint64_t throttle_revision = 0;
    bool burst_drop_enabled = false;
    float burst_drop_rate = 0.0F;
    int burst_drop_length = kDefaultBurstDropLength;
    float drop_rate = 0.0F;
    float duplicate_rate = 0.0F;
    int duplicate_count = kDefaultDuplicateCopies;
};

class DirectionEffectConfig
{
public:
    [[nodiscard]] DirectionEffectSnapshot Snapshot() const
    {
        const auto delay_ms = DelayMs();
        const auto delay_jitter_ms = DelayJitterMs();
        const auto throttle_bytes_per_second = ThrottleBytesPerSecond();
        const auto throttle_revision = ThrottleRevision();

        return {.delay = std::chrono::milliseconds(delay_ms),
                .delay_jitter_ms = delay_jitter_ms,
                .throttle_bytes_per_second = throttle_bytes_per_second,
                .throttle_revision = throttle_revision,
                .burst_drop_enabled = BurstDropEnabled(),
                .burst_drop_rate = BurstDropRate(),
                .burst_drop_length = BurstDropLength(),
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
        delay_ms_.store((std::max)(delay_ms, 0), std::memory_order_relaxed);
    }

    [[nodiscard]] int DelayJitterMs() const
    {
        return delay_jitter_ms_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::chrono::milliseconds DelayJitter() const
    {
        return std::chrono::milliseconds(DelayJitterMs());
    }

    void SetDelayJitterMs(int delay_jitter_ms)
    {
        delay_jitter_ms_.store(std::clamp(delay_jitter_ms, 0, kMaxDelayJitterMs), std::memory_order_relaxed);
    }

    [[nodiscard]] int ThrottleKBytesPerSec() const
    {
        return throttle_kbytes_per_sec_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t ThrottleBytesPerSecond() const
    {
        return static_cast<std::uint64_t>(ThrottleKBytesPerSec()) * 1024ULL;
    }

    [[nodiscard]] std::uint64_t ThrottleRevision() const
    {
        return throttle_revision_.load(std::memory_order_relaxed);
    }

    void SetThrottleKBytesPerSec(int throttle_kbytes_per_sec)
    {
        const int clamped = std::clamp(throttle_kbytes_per_sec, 0, kMaxThrottleKBytesPerSec);
        const int current = throttle_kbytes_per_sec_.load(std::memory_order_relaxed);
        if (current != clamped)
        {
            throttle_kbytes_per_sec_.store(clamped, std::memory_order_relaxed);
            throttle_revision_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool BurstDropEnabled() const
    {
        return burst_drop_enabled_.load(std::memory_order_relaxed);
    }

    void SetBurstDropEnabled(bool enabled)
    {
        burst_drop_enabled_.store(enabled, std::memory_order_relaxed);
    }

    [[nodiscard]] float BurstDropRate() const
    {
        return burst_drop_rate_.load(std::memory_order_relaxed);
    }

    void SetBurstDropRate(float burst_drop_rate)
    {
        burst_drop_rate_.store(std::clamp(burst_drop_rate, 0.0F, 1.0F), std::memory_order_relaxed);
    }

    [[nodiscard]] int BurstDropLength() const
    {
        return burst_drop_length_.load(std::memory_order_relaxed);
    }

    void SetBurstDropLength(int burst_drop_length)
    {
        burst_drop_length_.store(std::clamp(burst_drop_length, 1, kMaxBurstDropLength), std::memory_order_relaxed);
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
    std::atomic<int> delay_jitter_ms_{0};
    std::atomic<int> throttle_kbytes_per_sec_{0};
    std::atomic<std::uint64_t> throttle_revision_{0};
    std::atomic<bool> burst_drop_enabled_{false};
    std::atomic<float> burst_drop_rate_{0.0F};
    std::atomic<int> burst_drop_length_{kDefaultBurstDropLength};
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
        return ConfigForDirection(is_outbound).Snapshot();
    }

    [[nodiscard]] std::chrono::milliseconds Delay(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).Delay();
    }

    [[nodiscard]] std::chrono::milliseconds DelayJitter(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).DelayJitter();
    }

    [[nodiscard]] int ThrottleKBytesPerSec(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).ThrottleKBytesPerSec();
    }

    [[nodiscard]] bool BurstDropEnabled(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).BurstDropEnabled();
    }

    [[nodiscard]] float BurstDropRate(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).BurstDropRate();
    }

    [[nodiscard]] int BurstDropLength(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).BurstDropLength();
    }

    [[nodiscard]] float DropRate(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).DropRate();
    }

    [[nodiscard]] float DuplicateRate(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).DuplicateRate();
    }

    [[nodiscard]] int DuplicateCount(bool is_outbound) const
    {
        return ConfigForDirection(is_outbound).DuplicateCount();
    }

    void SetDelayMs(bool is_outbound, int delay_ms)
    {
        ConfigForDirection(is_outbound).SetDelayMs(delay_ms);
    }

    void SetDelayJitterMs(bool is_outbound, int delay_jitter_ms)
    {
        ConfigForDirection(is_outbound).SetDelayJitterMs(delay_jitter_ms);
    }

    void SetThrottleKBytesPerSec(bool is_outbound, int throttle_kbytes_per_sec)
    {
        ConfigForDirection(is_outbound).SetThrottleKBytesPerSec(throttle_kbytes_per_sec);
    }

    void SetBurstDropEnabled(bool is_outbound, bool enabled)
    {
        ConfigForDirection(is_outbound).SetBurstDropEnabled(enabled);
    }

    void SetBurstDropRate(bool is_outbound, float burst_drop_rate)
    {
        ConfigForDirection(is_outbound).SetBurstDropRate(burst_drop_rate);
    }

    void SetBurstDropLength(bool is_outbound, int burst_drop_length)
    {
        ConfigForDirection(is_outbound).SetBurstDropLength(burst_drop_length);
    }

    void SetDropRate(bool is_outbound, float drop_rate)
    {
        ConfigForDirection(is_outbound).SetDropRate(drop_rate);
    }

    void SetDuplicateRate(bool is_outbound, float duplicate_rate)
    {
        ConfigForDirection(is_outbound).SetDuplicateRate(duplicate_rate);
    }

    void SetDuplicateCount(bool is_outbound, int duplicate_count)
    {
        ConfigForDirection(is_outbound).SetDuplicateCount(duplicate_count);
    }

    [[nodiscard]] DirectionEffectConfig& ConfigForDirection(bool is_outbound)
    {
        return is_outbound ? outbound : inbound;
    }

    [[nodiscard]] const DirectionEffectConfig& ConfigForDirection(bool is_outbound) const
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

class TargetPidSet
{
public:
    void Add(uint32_t pid)
    {
        const std::scoped_lock lock(mutex_);
        pids_.insert(pid);
        RefreshCachedPidLocked();
    }

    void Remove(uint32_t pid)
    {
        const std::scoped_lock lock(mutex_);
        pids_.erase(pid);
        RefreshCachedPidLocked();
    }

    void Clear()
    {
        const std::scoped_lock lock(mutex_);
        pids_.clear();
        cached_pid_.store(kNoCachedPid, std::memory_order_release);
    }

    void SetSelectedPid(uint32_t pid)
    {
        const std::scoped_lock lock(mutex_);
        pids_.clear();
        pids_.insert(pid);
        cached_pid_.store(pid, std::memory_order_release);
    }

    bool Contains(uint32_t pid) const
    {
        const uint32_t cached_pid = cached_pid_.load(std::memory_order_acquire);
        if (cached_pid != kNoCachedPid)
        {
            return cached_pid == pid;
        }

        const std::scoped_lock lock(mutex_);
        return pids_.contains(pid);
    }

private:
    void RefreshCachedPidLocked()
    {
        if (pids_.size() == 1)
        {
            cached_pid_.store(*pids_.begin(), std::memory_order_release);
        }
        else
        {
            cached_pid_.store(kNoCachedPid, std::memory_order_release);
        }
    }

    static constexpr uint32_t kNoCachedPid = (std::numeric_limits<uint32_t>::max)();

    mutable std::mutex mutex_;
    std::unordered_set<uint32_t> pids_;
    std::atomic<uint32_t> cached_pid_{kNoCachedPid};
};

}  // namespace scrambler::core
