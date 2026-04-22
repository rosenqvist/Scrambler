#include "core/PacketData.h"
#include "core/ScheduledPacketQueue.h"

#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace
{

scrambler::core::ScheduledPacket MakePacket(uint32_t pid, std::chrono::steady_clock::time_point release_at)
{
    scrambler::core::ScheduledPacket packet;
    packet.packet.metadata.pid = pid;
    packet.release_at = release_at;
    return packet;
}

}  // namespace

TEST(PacketSchedulingTest, ReturnsReadyPacketsInScheduledOrder)
{
    scrambler::core::ScheduledPacketQueue queue;
    auto late = MakePacket(1, std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(30));
    auto early = MakePacket(2, std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(10));
    auto mid = MakePacket(3, std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(20));

    queue.Push(&late);
    queue.Push(&early);
    queue.Push(&mid);

    std::vector<uint32_t> ready_pids;
    queue.PopReady(std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(25),
                   8,
                   [&](auto* packet)
    {
        ready_pids.push_back(packet->packet.metadata.pid);
    });

    ASSERT_EQ(ready_pids.size(), 2U);
    EXPECT_EQ(ready_pids.at(0), 2U);
    EXPECT_EQ(ready_pids.at(1), 3U);
    ASSERT_NE(queue.Peek(), nullptr);
    EXPECT_EQ(queue.Peek()->packet.metadata.pid, 1U);
}

TEST(PacketSchedulingTest, ReturnsOnlyUpToTheRequestedNumberOfPackets)
{
    scrambler::core::ScheduledPacketQueue queue;
    auto first = MakePacket(10, std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(5));
    auto second = MakePacket(11, std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(6));

    queue.Push(&first);
    queue.Push(&second);

    std::vector<uint32_t> ready_pids;
    const auto popped = queue.PopReady(std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(10),
                                       1,
                                       [&](auto* packet)
    {
        ready_pids.push_back(packet->packet.metadata.pid);
    });

    EXPECT_EQ(popped, 1U);
    ASSERT_EQ(ready_pids.size(), 1U);
    EXPECT_EQ(ready_pids.at(0), 10U);
    ASSERT_NE(queue.Peek(), nullptr);
    EXPECT_EQ(queue.Peek()->packet.metadata.pid, 11U);
}

TEST(PacketSchedulingTest, DrainsAllRemainingPacketsInScheduledOrder)
{
    scrambler::core::ScheduledPacketQueue queue;
    auto first = MakePacket(20, std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(20));
    auto second = MakePacket(21, std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(10));

    queue.Push(&first);
    queue.Push(&second);

    std::vector<uint32_t> drained_pids;
    queue.DrainAll([&](auto* packet)
    {
        drained_pids.push_back(packet->packet.metadata.pid);
    });

    ASSERT_EQ(drained_pids.size(), 2U);
    EXPECT_EQ(drained_pids.at(0), 21U);
    EXPECT_EQ(drained_pids.at(1), 20U);
    EXPECT_TRUE(queue.Empty());
}
