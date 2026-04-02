#include "core/DelayQueue.h"

namespace scrambler::core
{

DelayQueue::DelayQueue(HANDLE divert_handle) : divert_handle_(divert_handle), handoff_queue_(65536)
{
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

    auto release_time = std::chrono::steady_clock::now() + delay;

    // A lock-free push.
    // If the queue is completely full, try_push returns false and we drop the packet.
    // This prevents WinDivert's capture loop from ever being blocked
    if (!handoff_queue_.try_push(DelayedPacket(packet_data, addr, release_time)))
    {
        DEBUG_PRINT("[WARN] Handoff queue full, dropped packet to save interceptor loop!");
    }
}

// Helpers

void DelayQueue::FlushPackets()
{
    // Thread is joined by now, so it is safe to iterate and drain
    while (handoff_queue_.front())
    {
        Reinject(*handoff_queue_.front());
        handoff_queue_.pop();
    }

    while (!priority_queue_.empty())
    {
        Reinject(priority_queue_.top());
        priority_queue_.pop();
    }
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
    while (running_.load(std::memory_order_acquire))
    {
        // Drain all incoming packets from the SPSC handoff queue
        // front() returns a pointer if data exists, nullptr if empty
        while (handoff_queue_.front())
        {
            priority_queue_.push(*handoff_queue_.front());
            handoff_queue_.pop();  // Pop from SPSC after reading
        }

        // 2. Check if the top packet in the priority queue is ready to reinject
        auto now = std::chrono::steady_clock::now();
        while (!priority_queue_.empty() && priority_queue_.top().release_at <= now)
        {
            Reinject(priority_queue_.top());
            priority_queue_.pop();
        }

        // CPU Yielding
        // we use yield(). This tells the OS to run other threads if they need it
        // but instantly gives control back to this loop to check for new packets.
        std::this_thread::yield();
    }
}

}  // namespace scrambler::core
