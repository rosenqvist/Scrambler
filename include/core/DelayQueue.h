#pragma once

#include "core/PacketData.h"
#include "core/ScheduledPacketQueue.h"
#include "rigtorp/SPSCQueue.h"

#include <atomic>
#include <thread>
#include <vector>

namespace scrambler::core
{

// Schedules delayed packets for reinjection on a background thread.
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

    // Enqueue an already-owned packet for future reinjection.
    void Push(ScheduledPacket packet);

private:
    void DrainLoop();
    void Reinject(const ScheduledPacket& pkt);
    void FlushPackets();

    HANDLE divert_handle_;

    std::vector<ScheduledPacket> memory_pool_;

    rigtorp::SPSCQueue<ScheduledPacket*> free_queue_;
    rigtorp::SPSCQueue<ScheduledPacket*> handoff_queue_;
    ScheduledPacketQueue scheduled_packets_;

    std::jthread thread_;

    std::atomic<bool> running_{false};
};

}  // namespace scrambler::core
