#include "core/EffectConfig.h"

#include <gtest/gtest.h>

using scrambler::core::Direction;
using scrambler::core::EffectConfig;
using scrambler::core::ShouldDrop;

//  Delays

TEST(EffectConfigTest, DefaultDelayIsZero)
{
    EffectConfig effects;

    EXPECT_EQ(effects.Delay().count(), 0);
}

TEST(EffectConfigTest, DelayReflectsStoredValue)
{
    EffectConfig effects;
    effects.delay_ms.store(150);

    EXPECT_EQ(effects.Delay().count(), 150);
}

TEST(EffectConfigTest, DelayCanBeUpdatedAtRuntime)
{
    EffectConfig effects;
    effects.delay_ms.store(100);
    EXPECT_EQ(effects.Delay().count(), 100);

    effects.delay_ms.store(500);
    EXPECT_EQ(effects.Delay().count(), 500);
}

// Drop rate

TEST(EffectConfigTest, DefaultDropRateIsZero)
{
    EffectConfig effects;

    EXPECT_FLOAT_EQ(effects.drop_rate.load(), 0.0F);
}

TEST(EffectConfigTest, ZeroDropRateNeverDrops)
{
    for (int i = 0; i < 10000; ++i)
    {
        EXPECT_FALSE(ShouldDrop(0.0F));
    }
}

TEST(EffectConfigTest, NegativeDropRateNeverDrops)
{
    for (int i = 0; i < 1000; ++i)
    {
        EXPECT_FALSE(ShouldDrop(-0.5F));
    }
}

TEST(EffectConfigTest, FullDropRateAlwaysDrops)
{
    for (int i = 0; i < 10000; ++i)
    {
        EXPECT_TRUE(ShouldDrop(1.0F));
    }
}

TEST(EffectConfigTest, FiftyPercentDropRateIsRoughlyHalf)
{
    int dropped = 0;
    constexpr int kTrials = 100000;

    for (int i = 0; i < kTrials; ++i)
    {
        if (ShouldDrop(0.5F))
        {
            ++dropped;
        }
    }

    double ratio = static_cast<double>(dropped) / kTrials;
    EXPECT_GT(ratio, 0.45);
    EXPECT_LT(ratio, 0.55);
}

TEST(EffectConfigTest, TenPercentDropRateIsRoughlyTenPercent)
{
    int dropped = 0;
    constexpr int kTrials = 100000;

    for (int i = 0; i < kTrials; ++i)
    {
        if (ShouldDrop(0.1F))
        {
            ++dropped;
        }
    }

    double ratio = static_cast<double>(dropped) / kTrials;
    EXPECT_GT(ratio, 0.07);
    EXPECT_LT(ratio, 0.13);
}

//  Delay directions

TEST(EffectConfigTest, DefaultDelayDirectionIsBoth)
{
    EffectConfig effects;

    EXPECT_TRUE(effects.MatchesDelayDirection(true));
    EXPECT_TRUE(effects.MatchesDelayDirection(false));
}

TEST(EffectConfigTest, DelayOutboundMatchesOutboundOnly)
{
    EffectConfig effects;
    effects.delay_direction.store(Direction::kOutbound);

    EXPECT_TRUE(effects.MatchesDelayDirection(true));
    EXPECT_FALSE(effects.MatchesDelayDirection(false));
}

TEST(EffectConfigTest, DelayInboundMatchesInboundOnly)
{
    EffectConfig effects;
    effects.delay_direction.store(Direction::kInbound);

    EXPECT_FALSE(effects.MatchesDelayDirection(true));
    EXPECT_TRUE(effects.MatchesDelayDirection(false));
}

// Drop direction

TEST(EffectConfigTest, DefaultDropDirectionIsBoth)
{
    EffectConfig effects;

    EXPECT_TRUE(effects.MatchesDropDirection(true));
    EXPECT_TRUE(effects.MatchesDropDirection(false));
}

TEST(EffectConfigTest, DropOutboundMatchesOutboundOnly)
{
    EffectConfig effects;
    effects.drop_direction.store(Direction::kOutbound);

    EXPECT_TRUE(effects.MatchesDropDirection(true));
    EXPECT_FALSE(effects.MatchesDropDirection(false));
}

TEST(EffectConfigTest, DropInboundMatchesInboundOnly)
{
    EffectConfig effects;
    effects.drop_direction.store(Direction::kInbound);

    EXPECT_FALSE(effects.MatchesDropDirection(true));
    EXPECT_TRUE(effects.MatchesDropDirection(false));
}

//  Independent directions

TEST(EffectConfigTest, DelayAndDropDirectionsAreIndependent)
{
    EffectConfig effects;
    effects.delay_direction.store(Direction::kInbound);
    effects.drop_direction.store(Direction::kOutbound);

    EXPECT_FALSE(effects.MatchesDelayDirection(true));
    EXPECT_TRUE(effects.MatchesDelayDirection(false));
    EXPECT_TRUE(effects.MatchesDropDirection(true));
    EXPECT_FALSE(effects.MatchesDropDirection(false));
}

// Default State
TEST(EffectConfigTest, DefaultsAreInert)
{
    EffectConfig effects;

    EXPECT_EQ(effects.delay_ms.load(), 0);
    EXPECT_EQ(effects.drop_rate.load(), 0.0F);
    EXPECT_EQ(effects.delay_direction.load(), Direction::kBoth);
    EXPECT_EQ(effects.drop_direction.load(), Direction::kBoth);
    EXPECT_EQ(effects.Delay().count(), 0);
    EXPECT_FALSE(ShouldDrop(effects.drop_rate.load()));
}
