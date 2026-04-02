#pragma once

#include "core/Types.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>
#include <thread>

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
        // Copy up to 1500 bytes (mtu size) into the fixed array
        size_t copy_size = (std::min) (packet_data.size(), kStandardMtuSize);
        std::copy_n(packet_data.begin(), copy_size, data.begin());
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
    void ProcessExpiredPackets();

    HANDLE divert_handle_;
    std::deque<DelayedPacket> queue_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::jthread thread_;
    bool running_{false};
};

}  // namespace scrambler::core
