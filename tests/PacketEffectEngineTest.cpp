#include "core/PacketEffectEngine.h"

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
    packet.data[0] = 0xAA;
    packet.data[1] = 0xBB;
    packet.data[2] = 0xCC;
    packet.data[3] = 0xDD;
    packet.metadata.tuple = {
        .src_addr = 0x0a000001, .dst_addr = 0x0a000002, .src_port = 40000, .dst_port = 50000, .protocol = 17};
    packet.metadata.pid = 4242;
    packet.metadata.is_outbound = is_outbound;
    return packet;
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
    EXPECT_EQ(emission.immediate_packets[0].length, 4U);
    EXPECT_EQ(emission.immediate_packets[0].data[0], 0xAA);
    EXPECT_TRUE(emission.immediate_packets[0].metadata.is_outbound);
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
    EXPECT_EQ(emission.scheduled_packets[0].release_at, now + std::chrono::milliseconds(150));
    EXPECT_EQ(emission.scheduled_packets[0].packet.length, 4U);
    EXPECT_EQ(emission.scheduled_packets[0].packet.data[3], 0xDD);
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
    EXPECT_EQ(emission.immediate_packets[0].length, 4U);
    EXPECT_EQ(emission.immediate_packets[5].length, 4U);
    EXPECT_EQ(emission.immediate_packets[0].data[0], 0xAA);
    EXPECT_EQ(emission.immediate_packets[5].data[0], 0xAA);
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
    EXPECT_EQ(emission.scheduled_packets[0].release_at, now + std::chrono::milliseconds(150));
    EXPECT_EQ(emission.scheduled_packets[3].release_at, now + std::chrono::milliseconds(150));
    EXPECT_EQ(emission.scheduled_packets[0].packet.data[3], 0xDD);
    EXPECT_EQ(emission.scheduled_packets[3].packet.data[3], 0xDD);
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
    effects.SetDuplicateRate(true, 1.0F);
    effects.SetDuplicateCount(true, 2);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(10);
    const auto outbound_emission = engine.Process(MakePacket(true), now);
    const auto inbound_emission = engine.Process(MakePacket(false), now);

    EXPECT_EQ(outbound_emission.ImmediateCount(), 3U);
    EXPECT_EQ(outbound_emission.ScheduledCount(), 0U);
    EXPECT_EQ(inbound_emission.ImmediateCount(), 0U);
    ASSERT_EQ(inbound_emission.ScheduledCount(), 1U);
    EXPECT_EQ(inbound_emission.scheduled_packets[0].release_at, now + std::chrono::milliseconds(90));
}
