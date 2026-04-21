#pragma once

#include "core/PacketData.h"
#include "core/ScheduledPacketQueue.h"
#include "rigtorp/SPSCQueue.h"

#include <atomic>
#include <thread>
#include <vector>

namespace scrambler::core
{

// The DelayQueue class acts as a scheduled reinjection queue for packets that should not be sent immediately.
// It safely takes packets from the fast interceptor thread and sorts them by their target release time and injects them
// back into the network.
class DelayQueue
{
public:
    explicit DelayQueue(HANDLE divert_handle);
    ~DelayQueue();

    // We delete these copy and move constructors to prevent accidentally cloning the queue.
    // We only ever want one instance of this queue managing the active thread.
    DelayQueue(const DelayQueue&) = delete;
    DelayQueue& operator=(const DelayQueue&) = delete;
    DelayQueue(DelayQueue&&) = delete;
    DelayQueue& operator=(DelayQueue&&) = delete;

    void Start();
    void Stop();

    // This is the only function the main capture loop calls. It enqueues an
    // already-owned packet for a future reinjection time.
    void Push(ScheduledPacket packet);

private:
    // This is the background loop that constantly checks if any packets are ready to be sent.
    void DrainLoop();
    void Reinject(const ScheduledPacket& pkt);

    // This cleans up and releases any leftover packets when the application closes down.
    void FlushPackets();

    HANDLE divert_handle_;

    std::vector<ScheduledPacket> memory_pool_;

    rigtorp::SPSCQueue<ScheduledPacket*> free_queue_;

    // This is rigtorps ultra fast lock-free queue, used alot in HFT. It's pretty overkill, but hey why not.
    // It acts as a fast bridge between the thread capturing packets and the background drain thread.
    // Because it is lock-free it stops the main capture loop from ever getting stuck.
    rigtorp::SPSCQueue<ScheduledPacket*> handoff_queue_;
    ScheduledPacketQueue scheduled_packets_;

    std::jthread thread_;

    std::atomic<bool> running_{false};
};

}  // namespace scrambler::core
