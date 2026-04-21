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
    packet.metadata.tuple = {.src_addr = 0x0a000001,
                             .dst_addr = 0x0a000002,
                             .src_port = 40000,
                             .dst_port = 50000,
                             .protocol = 17};
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

    ASSERT_EQ(emission.immediate_count, 1U);
    EXPECT_EQ(emission.scheduled_count, 0U);
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

    EXPECT_EQ(emission.immediate_count, 0U);
    ASSERT_EQ(emission.scheduled_count, 1U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDelay));
    EXPECT_EQ(emission.scheduled_packets[0].release_at, now + std::chrono::milliseconds(150));
    EXPECT_EQ(emission.scheduled_packets[0].packet.length, 4U);
    EXPECT_EQ(emission.scheduled_packets[0].packet.data[3], 0xDD);
}

TEST(PacketEffectsTest, DropsPacketWhenDropRateIsOneHundredPercent)
{
    EffectConfig effects;
    effects.SetDropRate(false, 1.0F);
    PacketEffectEngine engine(effects);

    const auto emission = engine.Process(MakePacket(false), std::chrono::steady_clock::time_point{});

    EXPECT_EQ(emission.immediate_count, 0U);
    EXPECT_EQ(emission.scheduled_count, 0U);
    EXPECT_TRUE(emission.HasEffect(PacketEffectKind::kDrop));
}

TEST(PacketEffectsTest, AppliesOutboundAndInboundSettingsSeparately)
{
    EffectConfig effects;
    effects.SetDelayMs(false, 90);
    PacketEffectEngine engine(effects);

    const auto now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(10);
    const auto outbound_emission = engine.Process(MakePacket(true), now);
    const auto inbound_emission = engine.Process(MakePacket(false), now);

    EXPECT_EQ(outbound_emission.immediate_count, 1U);
    EXPECT_EQ(outbound_emission.scheduled_count, 0U);
    EXPECT_EQ(inbound_emission.immediate_count, 0U);
    ASSERT_EQ(inbound_emission.scheduled_count, 1U);
    EXPECT_EQ(inbound_emission.scheduled_packets[0].release_at, now + std::chrono::milliseconds(90));
}
