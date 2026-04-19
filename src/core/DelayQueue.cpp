#include "core/DelayQueue.h"

#include "core/Diagnostics.h"

namespace scrambler::core
{

DelayQueue::DelayQueue(HANDLE divert_handle)
    : divert_handle_(divert_handle),
      memory_pool_(kDelayQueueCapacity),
      free_queue_(kDelayQueueCapacity),
      handoff_queue_(kDelayQueueCapacity)
{
    for (size_t i = 0; i < kDelayQueueCapacity; ++i)
    {
        free_queue_.push(&memory_pool_[i]);
    }
}

DelayQueue::~DelayQueue()
{
    Stop();
}

void DelayQueue::Start()
{
    running_.store(true, std::memory_order_release);
    thread_ = std::jthread([this]
    {
        DrainLoop();
    });
}

void DelayQueue::Stop()
{
    // This instantly signals the DrainLoop to exit
    running_.store(false, std::memory_order_release);

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

    auto* free_pkt_ptr = free_queue_.front();

    // A lock-free push.
    // If the queue is completely full we drop the packet.
    // This prevents WinDivert's capture loop from ever being blocked
    if (!free_pkt_ptr)
    {
        static std::atomic<uint64_t> occurrences{0};
        LogRateLimited(occurrences,
                       LogLevel::kWarn,
                       "DelayQueue: free pool exhausted, dropping packet to avoid blocking capture loop");
        CountEvent(Counter::kPoolExhausted);
        return;
    }

    DelayedPacket* pkt = *free_pkt_ptr;
    free_queue_.pop();

    pkt->length = static_cast<UINT>(packet_data.size());
    pkt->addr = addr;
    pkt->release_at = std::chrono::steady_clock::now() + delay;
    std::memcpy(pkt->data.data(), packet_data.data(), pkt->length);

    handoff_queue_.push(pkt);
}

// Helpers

void DelayQueue::FlushPackets()
{
    // Thread is joined by now, so it is safe to iterate and drain
    while (handoff_queue_.front())
    {
        DelayedPacket* pkt = *handoff_queue_.front();
        Reinject(pkt);
        handoff_queue_.pop();
        free_queue_.push(pkt);
    }

    while (!priority_queue_.empty())
    {
        DelayedPacket* pkt = priority_queue_.top();
        Reinject(pkt);
        priority_queue_.pop();
        free_queue_.push(pkt);
    }
}

void DelayQueue::Reinject(const DelayedPacket* pkt)
{
    if (WinDivertSend(divert_handle_, pkt->data.data(), pkt->length, nullptr, &pkt->addr) == 0)
    {
        static std::atomic<uint64_t> occurrences{0};
        LogRateLimited(occurrences, LogLevel::kWarn, "DelayQueue: WinDivertSend failed (GLE={})", GetLastError());
        CountEvent(Counter::kReinjectFailures);
    }
}

void DelayQueue::DrainLoop()
{
    constexpr UINT kBatchSize = 128;
    constexpr UINT kBatchBufferLen = kBatchSize * kStandardMtuSize;

    std::vector<uint8_t> send_buf(kBatchBufferLen);
    std::vector<WINDIVERT_ADDRESS> send_addrs(kBatchSize);

    while (running_.load(std::memory_order_acquire))
    {
        bool did_work = false;

        // Transfer packets from the lock-free handoff queue
        while (handoff_queue_.front())
        {
            priority_queue_.push(*handoff_queue_.front());
            handoff_queue_.pop();
            did_work = true;
        }

        auto now = std::chrono::steady_clock::now();
        UINT send_len = 0;
        UINT send_count = 0;

        // Batch ready packets
        while (!priority_queue_.empty() && priority_queue_.top()->release_at <= now && send_count < kBatchSize)
        {
            DelayedPacket* pkt = priority_queue_.top();

            std::memcpy(send_buf.data() + send_len, pkt->data.data(), pkt->length);
            send_addrs[send_count] = pkt->addr;

            send_len += pkt->length;
            send_count++;

            priority_queue_.pop();
            free_queue_.push(pkt);
        }

        // Reinject batch
        if (send_count > 0)
        {
            did_work = true;
            if (WinDivertSendEx(divert_handle_,
                                send_buf.data(),
                                send_len,
                                nullptr,
                                0,
                                send_addrs.data(),
                                send_count * sizeof(WINDIVERT_ADDRESS),
                                nullptr)
                == 0)
            {
                static std::atomic<uint64_t> occurrences{0};
                LogRateLimited(occurrences,
                               LogLevel::kWarn,
                               "DelayQueue: batch WinDivertSendEx failed (GLE={}, packets={})",
                               GetLastError(),
                               send_count);
                CountEvent(Counter::kReinjectFailures, send_count);
            }
            else
            {
                CountEvent(Counter::kPacketsReinjected, send_count);
            }
        }

        // Sleep logic to prevent 100% cpu usage
        if (!did_work)
        {
            if (!priority_queue_.empty())
            {
                // We have packets waiting check how long until the next one is ready
                auto time_to_wait = priority_queue_.top()->release_at - std::chrono::steady_clock::now();

                // we can sleep for a 1 ms if the next packet is longer away than 1 ms
                if (time_to_wait > std::chrono::milliseconds(1))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                else
                {
                    // If its less than 1ms we have to yield to maintain precision
                    std::this_thread::yield();
                }
            }
            else
            {
                // Queue is completely empty now so its safe to sleep and free up the CPU core
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
}

}  // namespace scrambler::core
