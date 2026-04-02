#pragma once

#include "core/Types.h"
#include "rigtorp/SPSCQueue.h"

#include <queue>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <span>
#include <thread>
#include <vector>

namespace scrambler::core
{

// This struct holds all the info we need to store a packet while it waits for its delay to expire.
// It keeps a copy of the packet data, its length and its routing address so we can inject it later.
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
        size_t copy_size = std::min<size_t>(packet_data.size(), kStandardMtuSize);
        std::copy_n(packet_data.begin(), copy_size, data.begin());
    }

    // We need this operator so our priority queue knows how to sort the packets.
    // It tells the queue to automatically bubble the packet with the earliest release time to the top.
    bool operator>(const DelayedPacket& other) const
    {
        return release_at > other.release_at;
    }
};

// The DelayQueue class acts as a holding area for delayed packets.
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

    // This is the only function the main capture loop calls. It simply drops a packet in the handoff queue.
    void Push(std::span<const uint8_t> packet_data, const WINDIVERT_ADDRESS& addr, std::chrono::milliseconds delay);

private:
    // This is the background loop that constantly checks if any packets are ready to be sent.
    void DrainLoop();
    void Reinject(const DelayedPacket& pkt);

    // This cleans up and releases any leftover packets when the application closes down.
    void FlushPackets();

    HANDLE divert_handle_;

    // This is rigtorps ultra fast lock-free queue, used alot in HFT. It's pretty overkill, but hey why not.
    // It acts as a fast bridge between the thread capturing packets and the background drain thread.
    // Because it is lock-free it stops the main capture loop from ever getting stuck.
    rigtorp::SPSCQueue<DelayedPacket> handoff_queue_;

    // This is where packets wait until their delay timer runs out.
    std::priority_queue<DelayedPacket, std::vector<DelayedPacket>, std::greater<DelayedPacket>> priority_queue_;

    std::jthread thread_;

    std::atomic<bool> running_{false};
};

}  // namespace scrambler::core
