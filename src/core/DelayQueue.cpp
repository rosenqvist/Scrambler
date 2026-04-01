#include "core/DelayQueue.h"

#include <cstring>
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
    {
        std::lock_guard lock(mutex_);
        running_ = false;
    }
    cv_.notify_one();

    if (thread_.joinable())
    {
        thread_.join();
    }

    // Flush anything still queued so packets aren't silently lost on shutdown
    std::lock_guard lock(mutex_);
    for (auto& pkt : queue_)
    {
        Reinject(pkt);
    }
    queue_.clear();
}

void DelayQueue::Push(const uint8_t* data, UINT len, const WINDIVERT_ADDRESS& addr, std::chrono::milliseconds delay)
{
    DelayedPacket pkt{};
    std::memcpy(pkt.data.data(), data, len);
    pkt.length = len;
    pkt.addr = addr;
    pkt.release_at = std::chrono::steady_clock::now() + delay;

    {
        std::lock_guard lock(mutex_);
        queue_.push_back(pkt);
    }
    cv_.notify_one();
}

void DelayQueue::Reinject(const DelayedPacket& pkt)
{
    if (WinDivertSend(divert_handle_, pkt.data.data(), pkt.length, nullptr, &pkt.addr) == 0)
    {
        std::println("[WARN] DelayQueue reinject failed: {}", GetLastError());
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

        // Sleep until the front packet's release time or until
        // a new packet arrives that might need to go out sooner
        auto next = queue_.front().release_at;

        cv_.wait_until(lock,
                       next,
                       [this]
        {
            return !running_ || (!queue_.empty() && queue_.front().release_at <= std::chrono::steady_clock::now());
        });

        if (!running_)
        {
            break;
        }

        // Reinject everything that's due in one batch to avoid
        // waking up once per packet when many expire at the same time
        auto now = std::chrono::steady_clock::now();
        while (!queue_.empty() && queue_.front().release_at <= now)
        {
            Reinject(queue_.front());
            queue_.pop_front();
        }
    }
}

}  // namespace scrambler::core
