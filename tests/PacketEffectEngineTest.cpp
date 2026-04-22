#include "core/EffectConfig.h"
#include "core/PacketData.h"
#include "core/PacketEffectEngine.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>

using scrambler::core::EffectConfig;
using scrambler::core::OwnedPacket;
using scrambler::core::PacketEffectEngine;
using scrambler::core::PacketEffectKind;

namespace
{

OwnedPacket MakePacket(bool is_outbound)
{
    OwnedPacket packet;
    packet.length = 4;
    packet.data.at(0) = 0xAA;
    packet.data.at(1) = 0xBB;
    packet.data.at(2) = 0xCC;
    packet.data.at(3) = 0xDD;
    packet.metadata.tuple = {
        .src_addr = 0x0a000001, .dst_addr = 0x0a000002, .src_port = 40000, .dst_port = 50000, .protocol = 17};
    packet.metadata.pid = 4242;
    packet.metadata.is_outbound = is_outbound;
    return packet;
}

std::chrono::steady_clock::duration ExpectedTransmissionTime(size_t packet_length, int throttle_kbytes_per_sec)
{
    const auto throttle_bytes_per_second = static_cast<std::uint64_t>(throttle_kbytes_per_sec) * 1024ULL;
    const auto packet_bytes = static_cast<std::uint64_t>(packet_length);
    const auto transmission_nanos =
        ((packet_bytes * 1'000'000'000ULL) + throttle_bytes_per_second - 1ULL) / throttle_bytes_per_second;
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::nanoseconds(transmission_nanos));
}

}  // namespace

TEST(PacketEffectsTest, SendsPacketImmediatelyWhenNoEffectsAreEnabled)
{
    EffectConfig effects;
    PacketEffectEngine engine(effects);

    const auto emission = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});

    ASSERT_EQ(emission.ImmediateCount(), 1U);
    EXPECT_EQ(emission.ScheduledCount(), 0U);
    EXPECT_FALSE(emission.HasEffect(PacketEffectKind::kDrop));
    EXPECT_FALSE(emission.HasEffect(PacketEffectKind::kDelay));
    EXPECT_EQ(emission.immediate_packets.at(0).length, 4U);
    EXPECT_EQ(emission.immediate_packets.at(0).data.at(0), 0xAA);
    EXPECT_TRUE(emission.immediate_packets.at(0).metadata.is_outbound);
}

TEST(PacketEffectsTest, SchedulesPacketForLaterWhenDelayIsEnabled)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 150);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto emission = engine.Process(MakePacket(true), now);

    EXPECT_EQ(emission.ImmediateCount(), 0U);
    ASSERT_EQ(emission.ScheduledCount(), 1U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelay));
    EXPECT_EQ(emission.scheduled_packets.at(0).release_at, now + std::chrono::milliseconds(150));
    EXPECT_EQ(emission.scheduled_packets.at(0).packet.length, 4U);
    EXPECT_EQ(emission.scheduled_packets.at(0).packet.data.at(3), 0xDD);
}

TEST(PacketEffectsTest, AddsJitterOnTopOfConfiguredDelay)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 120);
    effects.SetDelayJitterMs(true, 30);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto emission = engine.Process(MakePacket(true), now);
    const auto scheduled_delay =
        std::chrono::duration_cast<std::chrono::milliseconds>(emission.scheduled_packets.at(0).release_at - now);

    EXPECT_EQ(emission.ImmediateCount(), 0U);
    ASSERT_EQ(emission.ScheduledCount(), 1U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelay));
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelayJitter));
    EXPECT_GE(scheduled_delay.count(), 120);
    EXPECT_LE(scheduled_delay.count(), 150);
}

TEST(PacketEffectsTest, SchedulesPacketForLaterWhenJitterIsEnabled)
{
    EffectConfig effects;
    effects.SetDelayJitterMs(true, 40);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto emission = engine.Process(MakePacket(true), now);
    const auto scheduled_delay =
        std::chrono::duration_cast<std::chrono::milliseconds>(emission.scheduled_packets.at(0).release_at - now);

    EXPECT_EQ(emission.ImmediateCount(), 0U);
    ASSERT_EQ(emission.ScheduledCount(), 1U);
    EXPECT_FALSE(emission.HasEffect(PacketEffectKind::kDelay));
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelayJitter));
    EXPECT_GE(scheduled_delay.count(), 0);
    EXPECT_LE(scheduled_delay.count(), 40);
}

TEST(PacketEffectsTest, BurstLossDropsTheConfiguredNumberOfPackets)
{
    EffectConfig effects;
    effects.SetBurstDropEnabled(true, true);
    effects.SetBurstDropRate(true, 1.0F);
    effects.SetBurstDropLength(true, 3);
    PacketEffectEngine engine(effects);

    const auto first = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});
    effects.SetBurstDropRate(true, 0.0F);
    const auto second = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});
    const auto third = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});
    const auto fourth = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});

    EXPECT_EQ(first.ImmediateCount(), 0U);
    EXPECT_EQ(first.ScheduledCount(), 0U);
    EXPECT_TRUE(first.HasEffect(PacketEffectKind::kDrop));
    EXPECT_TRUE(first.HasEffect(PacketEffectKind::kBurstDrop));

    EXPECT_EQ(second.ImmediateCount(), 0U);
    EXPECT_EQ(second.ScheduledCount(), 0U);
    EXPECT_TRUE(second.HasEffect(PacketEffectKind::kDrop));
    EXPECT_TRUE(second.HasEffect(PacketEffectKind::kBurstDrop));

    EXPECT_EQ(third.ImmediateCount(), 0U);
    EXPECT_EQ(third.ScheduledCount(), 0U);
    EXPECT_TRUE(third.HasEffect(PacketEffectKind::kDrop));
    EXPECT_TRUE(third.HasEffect(PacketEffectKind::kBurstDrop));

    ASSERT_EQ(fourth.ImmediateCount(), 1U);
    EXPECT_EQ(fourth.ScheduledCount(), 0U);
    EXPECT_FALSE(fourth.HasEffect(PacketEffectKind::kDrop));
    EXPECT_FALSE(fourth.HasEffect(PacketEffectKind::kBurstDrop));
}

TEST(PacketEffectsTest, BurstLossModeSuppressesRandomDropSettings)
{
    EffectConfig effects;
    effects.SetDropRate(true, 1.0F);
    effects.SetBurstDropEnabled(true, true);
    effects.SetBurstDropRate(true, 0.0F);
    effects.SetBurstDropLength(true, 4);
    PacketEffectEngine engine(effects);

    const auto emission = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});

    ASSERT_EQ(emission.ImmediateCount(), 1U);
    EXPECT_EQ(emission.ScheduledCount(), 0U);
    EXPECT_FALSE(emission.HasEffect(PacketEffectKind::kDrop));
    EXPECT_FALSE(emission.HasEffect(PacketEffectKind::kBurstDrop));
}

TEST(PacketEffectsTest, ThrottleDelaysLaterPacketsWhenBacklogBuilds)
{
    EffectConfig effects;
    effects.SetThrottleKBytesPerSec(true, 1);
    PacketEffectEngine engine(effects);
    const auto transmission_time = ExpectedTransmissionTime(MakePacket(true).length, 1);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto first = engine.Process(MakePacket(true), now);
    const auto second = engine.Process(MakePacket(true), now);

    ASSERT_EQ(first.ImmediateCount(), 1U);
    EXPECT_EQ(first.ScheduledCount(), 0U);
    EXPECT_FALSE(first.HasEffect(PacketEffectKind::kBandwidthThrottle));

    EXPECT_EQ(second.ImmediateCount(), 0U);
    ASSERT_EQ(second.ScheduledCount(), 1U);
    EXPECT_TRUE(second.HasEffect(PacketEffectKind::kBandwidthThrottle));
    EXPECT_EQ(second.scheduled_packets.at(0).release_at, now + transmission_time);
}

TEST(PacketEffectsTest, ThrottleStacksOnTopOfDelay)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 50);
    effects.SetThrottleKBytesPerSec(true, 1);
    PacketEffectEngine engine(effects);
    const auto transmission_time = ExpectedTransmissionTime(MakePacket(true).length, 1);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto first = engine.Process(MakePacket(true), now);
    const auto second = engine.Process(MakePacket(true), now);

    EXPECT_EQ(first.ImmediateCount(), 0U);
    ASSERT_EQ(first.ScheduledCount(), 1U);
    EXPECT_TRUE(first.HasEffect(PacketEffectKind::kDelay));
    EXPECT_FALSE(first.HasEffect(PacketEffectKind::kBandwidthThrottle));
    EXPECT_EQ(first.scheduled_packets.at(0).release_at, now + std::chrono::milliseconds(50));

    EXPECT_EQ(second.ImmediateCount(), 0U);
    ASSERT_EQ(second.ScheduledCount(), 1U);
    EXPECT_TRUE(second.HasEffect(PacketEffectKind::kDelay));
    EXPECT_TRUE(second.HasEffect(PacketEffectKind::kBandwidthThrottle));
    EXPECT_EQ(second.scheduled_packets.at(0).release_at, now + std::chrono::milliseconds(50) + transmission_time);
}

TEST(PacketEffectsTest, ReEnablingThrottleClearsPreviousBacklog)
{
    EffectConfig effects;
    effects.SetThrottleKBytesPerSec(true, 1);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto first = engine.Process(MakePacket(true), now);
    const auto second = engine.Process(MakePacket(true), now);

    ASSERT_EQ(first.ImmediateCount(), 1U);
    ASSERT_EQ(second.ScheduledCount(), 1U);

    effects.SetThrottleKBytesPerSec(true, 0);
    effects.SetThrottleKBytesPerSec(true, 1);

    const auto third = engine.Process(MakePacket(true), now);

    EXPECT_EQ(third.ImmediateCount(), 1U);
    EXPECT_EQ(third.ScheduledCount(), 0U);
    EXPECT_FALSE(third.HasEffect(PacketEffectKind::kBandwidthThrottle));
}

TEST(PacketEffectsTest, ThrottleSpreadsDuplicatedCopiesOverTime)
{
    EffectConfig effects;
    effects.SetThrottleKBytesPerSec(true, 1);
    effects.SetDuplicateRate(true, 1.0F);
    effects.SetDuplicateCount(true, 2);
    PacketEffectEngine engine(effects);
    const auto transmission_time = ExpectedTransmissionTime(MakePacket(true).length, 1);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto emission = engine.Process(MakePacket(true), now);

    ASSERT_EQ(emission.ImmediateCount(), 1U);
    ASSERT_EQ(emission.ScheduledCount(), 2U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDuplicate));
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kBandwidthThrottle));
    EXPECT_EQ(emission.scheduled_packets.at(0).release_at, now + transmission_time);
    EXPECT_EQ(emission.scheduled_packets.at(1).release_at, now + (transmission_time * 2));
}

TEST(PacketEffectsTest, SendsOneExtraCopyByDefaultWhenDuplicationIsEnabled)
{
    EffectConfig effects;
    effects.SetDuplicateRate(true, 1.0F);
    PacketEffectEngine engine(effects);

    const auto emission = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});

    ASSERT_EQ(emission.ImmediateCount(), 2U);
    EXPECT_EQ(emission.ScheduledCount(), 0U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDuplicate));
}

TEST(PacketEffectsTest, SendsConfiguredNumberOfCopiesWhenDuplicationIsEnabled)
{
    EffectConfig effects;
    effects.SetDuplicateRate(true, 1.0F);
    effects.SetDuplicateCount(true, 5);
    PacketEffectEngine engine(effects);

    const auto emission = engine.Process(MakePacket(true), std::chrono::steady_clock::time_point{});

    ASSERT_EQ(emission.ImmediateCount(), 6U);
    EXPECT_EQ(emission.ScheduledCount(), 0U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDuplicate));
    EXPECT_EQ(emission.immediate_packets.at(0).length, 4U);
    EXPECT_EQ(emission.immediate_packets.at(5).length, 4U);
    EXPECT_EQ(emission.immediate_packets.at(0).data.at(0), 0xAA);
    EXPECT_EQ(emission.immediate_packets.at(5).data.at(0), 0xAA);
}

TEST(PacketEffectsTest, SchedulesConfiguredNumberOfCopiesWhenDelayAndDuplicationAreEnabled)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 150);
    effects.SetDuplicateRate(true, 1.0F);
    effects.SetDuplicateCount(true, 3);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto emission = engine.Process(MakePacket(true), now);

    EXPECT_EQ(emission.ImmediateCount(), 0U);
    ASSERT_EQ(emission.ScheduledCount(), 4U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelay));
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDuplicate));
    EXPECT_EQ(emission.scheduled_packets.at(0).release_at, now + std::chrono::milliseconds(150));
    EXPECT_EQ(emission.scheduled_packets.at(3).release_at, now + std::chrono::milliseconds(150));
    EXPECT_EQ(emission.scheduled_packets.at(0).packet.data.at(3), 0xDD);
    EXPECT_EQ(emission.scheduled_packets.at(3).packet.data.at(3), 0xDD);
}

TEST(PacketEffectsTest, AppliesTheSameJitteredReleaseTimeToAllDuplicatedCopies)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 150);
    effects.SetDelayJitterMs(true, 25);
    effects.SetDuplicateRate(true, 1.0F);
    effects.SetDuplicateCount(true, 2);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(250);
    const auto emission = engine.Process(MakePacket(true), now);
    const auto scheduled_delay =
        std::chrono::duration_cast<std::chrono::milliseconds>(emission.scheduled_packets.at(0).release_at - now);

    EXPECT_EQ(emission.ImmediateCount(), 0U);
    ASSERT_EQ(emission.ScheduledCount(), 3U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelay));
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelayJitter));
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDuplicate));
    EXPECT_EQ(emission.scheduled_packets.at(1).release_at, emission.scheduled_packets.at(0).release_at);
    EXPECT_EQ(emission.scheduled_packets.at(2).release_at, emission.scheduled_packets.at(0).release_at);
    EXPECT_GE(scheduled_delay.count(), 150);
    EXPECT_LE(scheduled_delay.count(), 175);
}

TEST(PacketEffectsTest, DropsPacketWhenDropRateIsOneHundredPercent)
{
    EffectConfig effects;
    effects.SetDropRate(false, 1.0F);
    PacketEffectEngine engine(effects);

    const auto emission = engine.Process(MakePacket(false), std::chrono::steady_clock::time_point{});

    EXPECT_EQ(emission.ImmediateCount(), 0U);
    EXPECT_EQ(emission.ScheduledCount(), 0U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDrop));
}

TEST(PacketEffectsTest, DropTakesPriorityOverDuplication)
{
    EffectConfig effects;
    effects.SetDropRate(false, 1.0F);
    effects.SetDuplicateRate(false, 1.0F);
    effects.SetDuplicateCount(false, 4);
    PacketEffectEngine engine(effects);

    const auto emission = engine.Process(MakePacket(false), std::chrono::steady_clock::time_point{});

    EXPECT_EQ(emission.ImmediateCount(), 0U);
    EXPECT_EQ(emission.ScheduledCount(), 0U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDrop));
    EXPECT_FALSE(emission.HasEffect(PacketEffectKind::kDuplicate));
}

TEST(PacketEffectsTest, AppliesOutboundAndInboundSettingsSeparately)
{
    EffectConfig effects;
    effects.SetDelayMs(false, 90);
    effects.SetBurstDropEnabled(true, true);
    effects.SetBurstDropRate(true, 1.0F);
    effects.SetBurstDropLength(true, 2);
    effects.SetDuplicateRate(true, 1.0F);
    effects.SetDuplicateCount(true, 2);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(10);
    const auto outbound_emission = engine.Process(MakePacket(true), now);
    const auto inbound_emission = engine.Process(MakePacket(false), now);

    EXPECT_EQ(outbound_emission.ImmediateCount(), 0U);
    EXPECT_EQ(outbound_emission.ScheduledCount(), 0U);
    EXPECT_TRUE(outbound_emission.HasEffect(PacketEffectKind::kDrop));
    EXPECT_TRUE(outbound_emission.HasEffect(PacketEffectKind::kBurstDrop));
    EXPECT_EQ(inbound_emission.ImmediateCount(), 0U);
    ASSERT_EQ(inbound_emission.ScheduledCount(), 1U);
    EXPECT_EQ(inbound_emission.scheduled_packets.at(0).release_at, now + std::chrono::milliseconds(90));
}
