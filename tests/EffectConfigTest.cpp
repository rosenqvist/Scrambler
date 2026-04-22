#include "core/EffectConfig.h"

#include <gtest/gtest.h>

using scrambler::core::EffectConfig;
using scrambler::core::ShouldDrop;
using scrambler::core::kDefaultDuplicateCopies;
using scrambler::core::kMaxDelayJitterMs;
using scrambler::core::kMaxDuplicateCopies;

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
    effects.SetDelayMs(true, 150);
    effects.SetDelayMs(false, 275);

    EXPECT_EQ(effects.Delay(true).count(), 150);
    EXPECT_EQ(effects.Delay(false).count(), 275);
}

TEST(EffectConfigTest, DelayCanBeUpdatedAtRuntime)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 100);
    effects.SetDelayMs(false, 200);
    EXPECT_EQ(effects.Delay(true).count(), 100);
    EXPECT_EQ(effects.Delay(false).count(), 200);

    effects.SetDelayMs(true, 500);
    effects.SetDelayMs(false, 350);
    EXPECT_EQ(effects.Delay(true).count(), 500);
    EXPECT_EQ(effects.Delay(false).count(), 350);
}

TEST(EffectConfigTest, DefaultDelayJitterIsZero)
{
    EffectConfig effects;

    EXPECT_EQ(effects.DelayJitter(true).count(), 0);
    EXPECT_EQ(effects.DelayJitter(false).count(), 0);
}

TEST(EffectConfigTest, DelayJitterCanDifferByDirection)
{
    EffectConfig effects;
    effects.SetDelayJitterMs(true, 35);
    effects.SetDelayJitterMs(false, 90);

    EXPECT_EQ(effects.DelayJitter(true).count(), 35);
    EXPECT_EQ(effects.DelayJitter(false).count(), 90);
}

TEST(EffectConfigTest, DelayJitterIsClampedToSupportedMaximum)
{
    EffectConfig effects;
    effects.SetDelayJitterMs(true, kMaxDelayJitterMs + 50);
    effects.SetDelayJitterMs(false, -10);

    EXPECT_EQ(effects.DelayJitter(true).count(), kMaxDelayJitterMs);
    EXPECT_EQ(effects.DelayJitter(false).count(), 0);
}

// Drop rate

TEST(EffectConfigTest, DefaultDropRateIsZero)
{
    EffectConfig effects;

    EXPECT_FLOAT_EQ(effects.DropRate(true), 0.0F);
    EXPECT_FLOAT_EQ(effects.DropRate(false), 0.0F);
}

TEST(EffectConfigTest, DefaultDuplicateRateIsZero)
{
    EffectConfig effects;

    EXPECT_FLOAT_EQ(effects.DuplicateRate(true), 0.0F);
    EXPECT_FLOAT_EQ(effects.DuplicateRate(false), 0.0F);
}

TEST(EffectConfigTest, DefaultDuplicateCountIsOneExtraCopy)
{
    EffectConfig effects;

    EXPECT_EQ(effects.DuplicateCount(true), kDefaultDuplicateCopies);
    EXPECT_EQ(effects.DuplicateCount(false), kDefaultDuplicateCopies);
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
    effects.SetDropRate(true, 0.25F);
    effects.SetDropRate(false, 0.75F);

    EXPECT_FLOAT_EQ(effects.DropRate(true), 0.25F);
    EXPECT_FLOAT_EQ(effects.DropRate(false), 0.75F);
}

TEST(EffectConfigTest, DuplicateRatesCanDifferByDirection)
{
    EffectConfig effects;
    effects.SetDuplicateRate(true, 0.20F);
    effects.SetDuplicateRate(false, 0.60F);

    EXPECT_FLOAT_EQ(effects.DuplicateRate(true), 0.20F);
    EXPECT_FLOAT_EQ(effects.DuplicateRate(false), 0.60F);
}

TEST(EffectConfigTest, DuplicateCountsCanDifferByDirection)
{
    EffectConfig effects;
    effects.SetDuplicateCount(true, 3);
    effects.SetDuplicateCount(false, 7);

    EXPECT_EQ(effects.DuplicateCount(true), 3);
    EXPECT_EQ(effects.DuplicateCount(false), 7);
}

TEST(EffectConfigTest, DuplicateCountIsClampedToSupportedMaximum)
{
    EffectConfig effects;
    effects.SetDuplicateCount(true, kMaxDuplicateCopies + 10);
    effects.SetDuplicateCount(false, 0);

    EXPECT_EQ(effects.DuplicateCount(true), kMaxDuplicateCopies);
    EXPECT_EQ(effects.DuplicateCount(false), kDefaultDuplicateCopies);
}

TEST(EffectConfigTest, DelaysCanDifferByDirection)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 120);
    effects.SetDelayMs(false, 480);

    EXPECT_EQ(effects.Delay(true).count(), 120);
    EXPECT_EQ(effects.Delay(false).count(), 480);
}

TEST(EffectConfigTest, DelayAndDropValuesAreIndependentByDirection)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 80);
    effects.SetDelayMs(false, 260);
    effects.SetDropRate(true, 0.10F);
    effects.SetDropRate(false, 0.35F);

    EXPECT_EQ(effects.Delay(true).count(), 80);
    EXPECT_EQ(effects.Delay(false).count(), 260);
    EXPECT_FLOAT_EQ(effects.DropRate(true), 0.10F);
    EXPECT_FLOAT_EQ(effects.DropRate(false), 0.35F);
}

TEST(EffectConfigTest, DelayJitterDropAndDuplicateValuesAreIndependentByDirection)
{
    EffectConfig effects;
    effects.SetDelayMs(true, 80);
    effects.SetDelayMs(false, 260);
    effects.SetDelayJitterMs(true, 25);
    effects.SetDelayJitterMs(false, 70);
    effects.SetDropRate(true, 0.10F);
    effects.SetDropRate(false, 0.35F);
    effects.SetDuplicateRate(true, 0.15F);
    effects.SetDuplicateRate(false, 0.45F);
    effects.SetDuplicateCount(true, 2);
    effects.SetDuplicateCount(false, 5);

    EXPECT_EQ(effects.Delay(true).count(), 80);
    EXPECT_EQ(effects.Delay(false).count(), 260);
    EXPECT_EQ(effects.DelayJitter(true).count(), 25);
    EXPECT_EQ(effects.DelayJitter(false).count(), 70);
    EXPECT_FLOAT_EQ(effects.DropRate(true), 0.10F);
    EXPECT_FLOAT_EQ(effects.DropRate(false), 0.35F);
    EXPECT_FLOAT_EQ(effects.DuplicateRate(true), 0.15F);
    EXPECT_FLOAT_EQ(effects.DuplicateRate(false), 0.45F);
    EXPECT_EQ(effects.DuplicateCount(true), 2);
    EXPECT_EQ(effects.DuplicateCount(false), 5);
    EXPECT_EQ(effects.Snapshot(true).delay_jitter.count(), 25);
    EXPECT_EQ(effects.Snapshot(false).delay_jitter.count(), 70);
    EXPECT_FLOAT_EQ(effects.Snapshot(true).duplicate_rate, 0.15F);
    EXPECT_FLOAT_EQ(effects.Snapshot(false).duplicate_rate, 0.45F);
    EXPECT_EQ(effects.Snapshot(true).duplicate_count, 2);
    EXPECT_EQ(effects.Snapshot(false).duplicate_count, 5);
}

// Default State
TEST(EffectConfigTest, DefaultsAreInert)
{
    EffectConfig effects;

    EXPECT_EQ(effects.Direction(true).DelayMs(), 0);
    EXPECT_EQ(effects.Direction(false).DelayMs(), 0);
    EXPECT_EQ(effects.Direction(true).DropRate(), 0.0F);
    EXPECT_EQ(effects.Direction(false).DropRate(), 0.0F);
    EXPECT_EQ(effects.Direction(true).DuplicateRate(), 0.0F);
    EXPECT_EQ(effects.Direction(false).DuplicateRate(), 0.0F);
    EXPECT_EQ(effects.Direction(true).DuplicateCount(), kDefaultDuplicateCopies);
    EXPECT_EQ(effects.Direction(false).DuplicateCount(), kDefaultDuplicateCopies);
    EXPECT_EQ(effects.Delay(true).count(), 0);
    EXPECT_EQ(effects.Delay(false).count(), 0);
    EXPECT_EQ(effects.DelayJitter(true).count(), 0);
    EXPECT_EQ(effects.DelayJitter(false).count(), 0);
    EXPECT_FALSE(ShouldDrop(effects.DropRate(true)));
    EXPECT_FALSE(ShouldDrop(effects.DropRate(false)));
}
