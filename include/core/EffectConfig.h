#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <unordered_set>

namespace scrambler::core
{

struct EffectConfig
{
public:
    std::atomic<int> outbound_delay_ms{0};
    std::atomic<int> inbound_delay_ms{0};
    std::atomic<float> outbound_drop_rate{0.0F};
    std::atomic<float> inbound_drop_rate{0.0F};

    std::chrono::milliseconds Delay(bool is_outbound) const
    {
        return std::chrono::milliseconds(is_outbound ? outbound_delay_ms.load() : inbound_delay_ms.load());
    }

    float DropRate(bool is_outbound) const
    {
        return is_outbound ? outbound_drop_rate.load() : inbound_drop_rate.load();
    }
};

inline bool ShouldDrop(float rate)
{
    if (rate <= 0.0F)
    {
        return false;
    }

    thread_local std::mt19937 rng{std::random_device{}()};
    return std::uniform_real_distribution<float>(0.0F, 1.0F)(rng) < rate;
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
