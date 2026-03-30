#include "core/EffectConfig.h"

#include <gtest/gtest.h>

namespace scrambler::core
{

// Delay tests

TEST(EffectConfig, DefaultDelayIsZero)
{
    EffectConfig effects;

    EXPECT_EQ(effects.Delay().count(), 0);
}

TEST(EffectConfig, DelayReflectsStoredValue)
{
    EffectConfig effects;
    effects.delay_ms.store(150);

    EXPECT_EQ(effects.Delay().count(), 150);
}

TEST(EffectConfig, DelayCanBeUpdatedAtRuntime)
{
    EffectConfig effects;
    effects.delay_ms.store(100);
    EXPECT_EQ(effects.Delay().count(), 100);

    effects.delay_ms.store(500);
    EXPECT_EQ(effects.Delay().count(), 500);
}

//  Drop rate tests

TEST(EffectConfig, ZeroDropRateNeverDrops)
{
    EffectConfig effects;
    effects.drop_rate.store(0.0F);

    // Should never return true at 0%
    for (int i = 0; i < 10000; ++i)
    {
        EXPECT_FALSE(effects.ShouldDrop());
    }
}

TEST(EffectConfig, NegativeDropRateNeverDrops)
{
    EffectConfig effects;
    effects.drop_rate.store(-0.5F);

    for (int i = 0; i < 1000; ++i)
    {
        EXPECT_FALSE(effects.ShouldDrop());
    }
}

TEST(EffectConfig, FullDropRateAlwaysDrops)
{
    EffectConfig effects;
    effects.drop_rate.store(1.0F);

    for (int i = 0; i < 10000; ++i)
    {
        EXPECT_TRUE(effects.ShouldDrop());
    }
}

TEST(EffectConfig, FiftyPercentDropRateIsRoughlyHalf)
{
    EffectConfig effects;
    effects.drop_rate.store(0.5F);

    int dropped = 0;
    constexpr int kTrials = 100000;

    for (int i = 0; i < kTrials; ++i)
    {
        if (effects.ShouldDrop())
        {
            ++dropped;
        }
    }

    // With 100k trials at 50% we should be well within 45-55%
    double ratio = static_cast<double>(dropped) / kTrials;
    EXPECT_GT(ratio, 0.45);
    EXPECT_LT(ratio, 0.55);
}

TEST(EffectConfig, TenPercentDropRateIsRoughlyTenPercent)
{
    EffectConfig effects;
    effects.drop_rate.store(0.1F);

    int dropped = 0;
    constexpr int kTrials = 100000;

    for (int i = 0; i < kTrials; ++i)
    {
        if (effects.ShouldDrop())
        {
            ++dropped;
        }
    }

    double ratio = static_cast<double>(dropped) / kTrials;
    EXPECT_GT(ratio, 0.07);
    EXPECT_LT(ratio, 0.13);
}

TEST(EffectConfig, DefaultDropRateIsZero)
{
    EffectConfig effects;

    EXPECT_FLOAT_EQ(effects.drop_rate.load(), 0.0F);
}

}  // namespace scrambler::core
