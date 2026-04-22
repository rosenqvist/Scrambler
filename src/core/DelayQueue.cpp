#include "core/DelayQueue.h"

#include "core/Diagnostics.h"
#include "core/PacketData.h"
#include "core/Types.h"

#include <windivert.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <errhandlingapi.h>
#include <thread>
#include <timeapi.h>
#include <vector>

namespace scrambler::core
{

namespace
{
class TimerResolutionScope
{
public:
    TimerResolutionScope() noexcept : active_(timeBeginPeriod(kPeriodMs) == TIMERR_NOERROR)
    {
    }

    ~TimerResolutionScope()
    {
        if (active_)
        {
            timeEndPeriod(kPeriodMs);
        }
    }

    TimerResolutionScope(const TimerResolutionScope&) = delete;
    TimerResolutionScope& operator=(const TimerResolutionScope&) = delete;
    TimerResolutionScope(TimerResolutionScope&&) = delete;
    TimerResolutionScope& operator=(TimerResolutionScope&&) = delete;

private:
    static constexpr UINT kPeriodMs = 1;
    bool active_ = false;
};

}  // namespace

DelayQueue::DelayQueue(HANDLE divert_handle)
    : divert_handle_(divert_handle),
      memory_pool_(kDelayQueueCapacity),
      free_queue_(kDelayQueueCapacity),
      handoff_queue_(kDelayQueueCapacity)
{
    for (size_t i = 0; i < kDelayQueueCapacity; ++i)
    {
        free_queue_.push(&memory_pool_.at(i));
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
    running_.store(false, std::memory_order_release);

    if (thread_.joinable())
    {
        thread_.join();
    }

    FlushPackets();
}

void DelayQueue::Push(ScheduledPacket packet)
{
    auto* free_pkt_ptr = free_queue_.front();

    if (!free_pkt_ptr)
    {
        static std::atomic<uint64_t> occurrences{0};
        LogRateLimited(occurrences,
                       LogLevel::kWarn,
                       "DelayQueue: free pool exhausted, dropping packet to avoid blocking capture loop");
        CountEvent(Counter::kPoolExhausted);
        return;
    }

    ScheduledPacket* slot = *free_pkt_ptr;
    free_queue_.pop();
    *slot = packet;
    handoff_queue_.push(slot);
}

void DelayQueue::FlushPackets()
{
    while (handoff_queue_.front())
    {
        ScheduledPacket* packet = *handoff_queue_.front();
        Reinject(*packet);
        handoff_queue_.pop();
        free_queue_.push(packet);
    }

    scheduled_packets_.DrainAll([&](ScheduledPacket* packet)
    {
        Reinject(*packet);
        free_queue_.push(packet);
    });
}

void DelayQueue::Reinject(const ScheduledPacket& pkt)
{
    if (WinDivertSend(divert_handle_, pkt.packet.data.data(), pkt.packet.length, nullptr, &pkt.packet.addr) == 0)
    {
        static std::atomic<uint64_t> occurrences{0};
        LogRateLimited(occurrences, LogLevel::kWarn, "DelayQueue: WinDivertSend failed (GLE={})", GetLastError());
        CountEvent(Counter::kReinjectFailures);
    }
}

void DelayQueue::DrainLoop()
{
    const TimerResolutionScope timer_scope;

    constexpr UINT kBatchSize = 128;
    constexpr UINT kBatchBufferLen = kBatchSize * kStandardMtuSize;

    std::vector<uint8_t> send_buf(kBatchBufferLen);
    std::vector<WINDIVERT_ADDRESS> send_addrs(kBatchSize);
    std::array<ScheduledPacket*, kBatchSize> ready_packets{};

    while (running_.load(std::memory_order_acquire))
    {
        bool did_work = false;

        while (handoff_queue_.front())
        {
            scheduled_packets_.Push(*handoff_queue_.front());
            handoff_queue_.pop();
            did_work = true;
        }

        const auto now = std::chrono::steady_clock::now();
        UINT send_len = 0;
        UINT send_count = 0;

        scheduled_packets_.PopReady(now,
                                    kBatchSize,
                                    [&](ScheduledPacket* packet)
        {
            std::memcpy(send_buf.data() + send_len, packet->packet.data.data(), packet->packet.length);
            send_addrs.at(static_cast<size_t>(send_count)) = packet->packet.addr;
            ready_packets.at(static_cast<size_t>(send_count)) = packet;
            send_len += packet->packet.length;
            ++send_count;
        });

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

            for (UINT i = 0; i < send_count; ++i)
            {
                free_queue_.push(ready_packets.at(static_cast<size_t>(i)));
            }
        }

        if (!did_work)
        {
            if (const ScheduledPacket* next_packet = scheduled_packets_.Peek())
            {
                const auto time_to_wait = next_packet->release_at - std::chrono::steady_clock::now();
                if (time_to_wait > std::chrono::milliseconds(1))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                else
                {
                    std::this_thread::yield();
                }
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
}

}  // namespace scrambler::core
