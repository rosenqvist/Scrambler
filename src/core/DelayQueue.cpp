#include "core/DelayQueue.h"

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
    if (packet_data.size() > kStandardMtuSize)
    {
        return;
    }

    auto release_time = std::chrono::steady_clock::now() + delay;
    bool needs_wakeup = false;

    {
        std::scoped_lock lock(mutex_);
        bool was_empty = queue_.empty();

        // Use emplace for priority_queue
        queue_.emplace(packet_data, addr, release_time);

        // Wake the thread if the queue was empty OR if the new packet
        // expires sooner than the previous top packet (which might be currently waited on).
        if (was_empty || release_time < queue_.top().release_at)
        {
            needs_wakeup = true;
        }
    }

    if (needs_wakeup)
    {
        cv_.notify_one();
    }
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

    // pop elements one by one
    while (!queue_.empty())
    {
        Reinject(queue_.top());
        queue_.pop();
    }
}

// Pass the unique_lock in to safely manage state
void DelayQueue::ProcessExpiredPackets(std::unique_lock<std::mutex>& lock)
{
    auto now = std::chrono::steady_clock::now();
    std::vector<DelayedPacket> to_reinject;

    // Gather packets while locked using .top() and .pop()
    while (!queue_.empty() && queue_.top().release_at <= now)
    {
        to_reinject.push_back(queue_.top());
        queue_.pop();
    }

    // Safely unlock the unique_lock
    lock.unlock();

    // Reinject without blocking the capture thread
    for (const auto& pkt : to_reinject)
    {
        Reinject(pkt);
    }

    // Safely re-lock the unique_lock
    lock.lock();
}

void DelayQueue::Reinject(const DelayedPacket& pkt)
{
    if (WinDivertSend(divert_handle_, pkt.data.data(), pkt.length, nullptr, &pkt.addr) == 0)
    {
        DEBUG_PRINT("[WARN] Reinject failed: {}", GetLastError());
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

        // Wait based on the earliest expiring packet (.top())
        cv_.wait_until(lock,
                       queue_.top().release_at,
                       [this]
        {
            return !running_ || (!queue_.empty() && queue_.top().release_at <= std::chrono::steady_clock::now());
        });

        if (!running_)
        {
            break;
        }

        // Pass the lock to the processing function
        ProcessExpiredPackets(lock);
    }
}

}  // namespace scrambler::core
