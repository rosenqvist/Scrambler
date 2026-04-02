#pragma once

#include "core/Types.h"

#include <queue>  // Changed from <deque> to <queue>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace scrambler::core
{

struct DelayedPacket
{
    std::array<uint8_t, kStandardMtuSize> data{};
    UINT length;
    WINDIVERT_ADDRESS addr;
    std::chrono::steady_clock::time_point release_at;

    DelayedPacket(std::span<const uint8_t> packet_data,
                  const WINDIVERT_ADDRESS& a,
                  std::chrono::steady_clock::time_point rel)
        : length(static_cast<UINT>(packet_data.size())), addr(a), release_at(rel)
    {
        size_t copy_size = std::min<size_t>(packet_data.size(), kStandardMtuSize);
        std::copy_n(packet_data.begin(), copy_size, data.begin());
    }

    // Needed for std::priority_queue to act as a min-heap
    bool operator>(const DelayedPacket& other) const
    {
        return release_at > other.release_at;
    }
};

class DelayQueue
{
public:
    explicit DelayQueue(HANDLE divert_handle);
    ~DelayQueue();

    DelayQueue(const DelayQueue&) = delete;
    DelayQueue& operator=(const DelayQueue&) = delete;
    DelayQueue(DelayQueue&&) = delete;
    DelayQueue& operator=(DelayQueue&&) = delete;

    void Start();
    void Stop();
    void Push(std::span<const uint8_t> packet_data, const WINDIVERT_ADDRESS& addr, std::chrono::milliseconds delay);

private:
    void DrainLoop();
    void Reinject(const DelayedPacket& pkt);
    void SignalShutdown();
    void FlushPackets();

    // Pass the unique_lock by reference to safely unlock/lock
    void ProcessExpiredPackets(std::unique_lock<std::mutex>& lock);

    HANDLE divert_handle_;

    // Min-heap priority queue
    std::priority_queue<DelayedPacket, std::vector<DelayedPacket>, std::greater<DelayedPacket>> queue_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::jthread thread_;
    bool running_{false};
};

}  // namespace scrambler::core
