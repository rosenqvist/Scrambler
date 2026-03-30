#pragma once

#include "core/Types.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace scrambler::core
{

struct DelayedPacket
{
    std::array<uint8_t, kMaxPacketSize> data{};
    UINT length{};
    WINDIVERT_ADDRESS addr{};
    std::chrono::steady_clock::time_point release_at;
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
    void Push(const uint8_t* data, UINT len, const WINDIVERT_ADDRESS& addr, std::chrono::milliseconds delay);

private:
    void DrainLoop();
    void Reinject(const DelayedPacket& pkt);

    HANDLE divert_handle_;
    std::deque<DelayedPacket> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::jthread thread_;
    bool running_{false};
};

}  // namespace scrambler::core
