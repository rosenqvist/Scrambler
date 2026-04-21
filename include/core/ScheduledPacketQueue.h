#pragma once

#include "core/PacketData.h"

#include <queue>
#include <vector>

namespace scrambler::core
{

struct CompareScheduledPacketPtr
{
    bool operator()(const ScheduledPacket* a, const ScheduledPacket* b) const
    {
        return a->release_at > b->release_at;
    }
};

class ScheduledPacketQueue
{
public:
    void Push(ScheduledPacket* packet)
    {
        queue_.push(packet);
    }

    template <typename Fn>
    size_t PopReady(std::chrono::steady_clock::time_point now, size_t limit, Fn&& on_packet)
    {
        size_t popped = 0;
        while (!queue_.empty() && queue_.top()->release_at <= now && popped < limit)
        {
            ScheduledPacket* packet = queue_.top();
            queue_.pop();
            on_packet(packet);
            ++popped;
        }

        return popped;
    }

    template <typename Fn>
    void DrainAll(Fn&& on_packet)
    {
        while (!queue_.empty())
        {
            ScheduledPacket* packet = queue_.top();
            queue_.pop();
            on_packet(packet);
        }
    }

    [[nodiscard]] bool Empty() const
    {
        return queue_.empty();
    }

    [[nodiscard]] const ScheduledPacket* Peek() const
    {
        return queue_.empty() ? nullptr : queue_.top();
    }

private:
    std::priority_queue<ScheduledPacket*, std::vector<ScheduledPacket*>, CompareScheduledPacketPtr> queue_;
};

}  // namespace scrambler::core
