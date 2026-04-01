#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <unordered_set>
#include <winsock.h>

namespace scrambler::core
{

enum class Direction : std::uint8_t
{
    kOutbound = 0,
    kInbound = 1,
    kBoth = 2,
};

struct EffectConfig
{
public:
    std::atomic<int> delay_ms{0};
    std::atomic<float> drop_rate{0.0F};
    std::atomic<Direction> delay_direction{Direction::kBoth};
    std::atomic<Direction> drop_direction{Direction::kBoth};

    bool MatchesDelayDirection(bool is_outbound) const
    {
        return MatchesDirection(delay_direction.load(), is_outbound);
    }

    bool MatchesDropDirection(bool is_outbound) const
    {
        return MatchesDirection(drop_direction.load(), is_outbound);
    }

    std::chrono::milliseconds Delay() const
    {
        return std::chrono::milliseconds(delay_ms.load());
    }

private:
    static bool MatchesDirection(Direction dir, bool is_outbound)
    {
        if (dir == Direction::kBoth)
        {
            return true;
        }
        return (dir == Direction::kOutbound) == is_outbound;
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
