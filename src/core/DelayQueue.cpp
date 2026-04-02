#include "core/DelayQueue.h"

#include <print>

namespace scrambler::core
{

DelayQueue::DelayQueue(HANDLE divert_handle) : divert_handle_(divert_handle)
{
}

DelayQueue::~DelayQueue()
{
    Stop();
}

void DelayQueue::Start()
{
    {
        std::lock_guard lock(mutex_);
        running_ = true;
    }
    thread_ = std::jthread([this]
    {
        DrainLoop();
    });
}

void DelayQueue::Stop()
{
    SignalShutdown();

    if (thread_.joinable())
    {
        thread_.join();
    }

    FlushPackets();
}

void DelayQueue::Push(std::span<const uint8_t> packet_data,
                      const WINDIVERT_ADDRESS& addr,
                      std::chrono::milliseconds delay)
{
    if (packet_data.size() > kMaxPacketSize)
    {
        return;
    }

    auto release_time = std::chrono::steady_clock::now() + delay;

    {
        std::scoped_lock lock(mutex_);
        // Construct the packet directly inside deques memory
        queue_.emplace_back(packet_data, addr, release_time);
    }
    cv_.notify_one();
}

// Helpers

void DelayQueue::SignalShutdown()
{
    {
        std::lock_guard lock(mutex_);
        running_ = false;
    }
    cv_.notify_one();
}

void DelayQueue::FlushPackets()
{
    std::lock_guard lock(mutex_);
    for (const auto& pkt : queue_)
    {
        Reinject(pkt);
    }
    queue_.clear();
}

void DelayQueue::ProcessExpiredPackets()
{
    auto now = std::chrono::steady_clock::now();
    while (!queue_.empty() && queue_.front().release_at <= now)
    {
        Reinject(queue_.front());
        queue_.pop_front();
    }
}

void DelayQueue::Reinject(const DelayedPacket& pkt)
{
    if (WinDivertSend(divert_handle_, pkt.data.data(), pkt.length, nullptr, &pkt.addr) == 0)
    {
        std::println("[WARN] Reinject failed: {}", GetLastError());
    }
}

void DelayQueue::DrainLoop()
{
    std::unique_lock lock(mutex_);
    while (running_)
    {
        if (queue_.empty())
        {
            cv_.wait(lock,
                     [this]
            {
                return !queue_.empty() || !running_;
            });
            continue;
        }

        cv_.wait_until(lock,
                       queue_.front().release_at,
                       [this]
        {
            return !running_ || (!queue_.empty() && queue_.front().release_at <= std::chrono::steady_clock::now());
        });

        if (!running_)
        {
            break;
        }

        ProcessExpiredPackets();
    }
}

}  // namespace scrambler::core
