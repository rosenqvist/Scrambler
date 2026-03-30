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
    std::atomic<int> delay_ms{0};
    std::atomic<float> drop_rate{0.0F};

    std::chrono::milliseconds Delay() const
    {
        return std::chrono::milliseconds(delay_ms.load());
    }

    bool ShouldDrop() const
    {
        auto rate = drop_rate.load();
        if (rate <= 0.0F)
        {
            return false;
        }
        // thread_local so each capture thread gets its own RNG
        // without needing a mutex
        thread_local std::mt19937 rng{std::random_device{}()};
        return std::uniform_real_distribution<float>(0.0F, 1.0F)(rng) < rate;
    }
};

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
