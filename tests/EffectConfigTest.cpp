#include "core/EffectConfig.h"

#include <gtest/gtest.h>

using scrambler::core::EffectConfig;
using scrambler::core::ShouldDrop;

//  Delays

TEST(EffectConfigTest, DefaultDelayIsZero)
{
    EffectConfig effects;

    EXPECT_EQ(effects.Delay(true).count(), 0);
    EXPECT_EQ(effects.Delay(false).count(), 0);
}

TEST(EffectConfigTest, DelayReflectsStoredValue)
{
    EffectConfig effects;
    effects.outbound_delay_ms.store(150);
    effects.inbound_delay_ms.store(275);

    EXPECT_EQ(effects.Delay(true).count(), 150);
    EXPECT_EQ(effects.Delay(false).count(), 275);
}

TEST(EffectConfigTest, DelayCanBeUpdatedAtRuntime)
{
    EffectConfig effects;
    effects.outbound_delay_ms.store(100);
    effects.inbound_delay_ms.store(200);
    EXPECT_EQ(effects.Delay(true).count(), 100);
    EXPECT_EQ(effects.Delay(false).count(), 200);

    effects.outbound_delay_ms.store(500);
    effects.inbound_delay_ms.store(350);
    EXPECT_EQ(effects.Delay(true).count(), 500);
    EXPECT_EQ(effects.Delay(false).count(), 350);
}

// Drop rate

TEST(EffectConfigTest, DefaultDropRateIsZero)
{
    EffectConfig effects;

    EXPECT_FLOAT_EQ(effects.DropRate(true), 0.0F);
    EXPECT_FLOAT_EQ(effects.DropRate(false), 0.0F);
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

TEST(EffectConfigTest, DropRatesCanDifferByDirection)
{
    EffectConfig effects;
    effects.outbound_drop_rate.store(0.25F);
    effects.inbound_drop_rate.store(0.75F);

    EXPECT_FLOAT_EQ(effects.DropRate(true), 0.25F);
    EXPECT_FLOAT_EQ(effects.DropRate(false), 0.75F);
}

TEST(EffectConfigTest, DelaysCanDifferByDirection)
{
    EffectConfig effects;
    effects.outbound_delay_ms.store(120);
    effects.inbound_delay_ms.store(480);

    EXPECT_EQ(effects.Delay(true).count(), 120);
    EXPECT_EQ(effects.Delay(false).count(), 480);
}

TEST(EffectConfigTest, DelayAndDropValuesAreIndependentByDirection)
{
    EffectConfig effects;
    effects.outbound_delay_ms.store(80);
    effects.inbound_delay_ms.store(260);
    effects.outbound_drop_rate.store(0.10F);
    effects.inbound_drop_rate.store(0.35F);

    EXPECT_EQ(effects.Delay(true).count(), 80);
    EXPECT_EQ(effects.Delay(false).count(), 260);
    EXPECT_FLOAT_EQ(effects.DropRate(true), 0.10F);
    EXPECT_FLOAT_EQ(effects.DropRate(false), 0.35F);
}

// Default State
TEST(EffectConfigTest, DefaultsAreInert)
{
    EffectConfig effects;

    EXPECT_EQ(effects.outbound_delay_ms.load(), 0);
    EXPECT_EQ(effects.inbound_delay_ms.load(), 0);
    EXPECT_EQ(effects.outbound_drop_rate.load(), 0.0F);
    EXPECT_EQ(effects.inbound_drop_rate.load(), 0.0F);
    EXPECT_EQ(effects.Delay(true).count(), 0);
    EXPECT_EQ(effects.Delay(false).count(), 0);
    EXPECT_FALSE(ShouldDrop(effects.DropRate(true)));
    EXPECT_FALSE(ShouldDrop(effects.DropRate(false)));
}
