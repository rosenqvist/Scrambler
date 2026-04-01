#include "core/EffectConfig.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using scrambler::core::TargetSet;

// Basic operations

TEST(TargetSetTest, EmptySetContainsNothing)
{
    TargetSet targets;

    EXPECT_FALSE(targets.Contains(1));
    EXPECT_FALSE(targets.Contains(0));
    EXPECT_FALSE(targets.Contains(99999));
}

TEST(TargetSetTest, AddedPidIsFound)
{
    TargetSet targets;
    targets.Add(1234);

    EXPECT_TRUE(targets.Contains(1234));
}

TEST(TargetSetTest, UnrelatedPidIsNotFound)
{
    TargetSet targets;
    targets.Add(1234);

    EXPECT_FALSE(targets.Contains(5678));
}

TEST(TargetSetTest, MultiplePidsCoexist)
{
    TargetSet targets;
    targets.Add(100);
    targets.Add(200);
    targets.Add(300);

    EXPECT_TRUE(targets.Contains(100));
    EXPECT_TRUE(targets.Contains(200));
    EXPECT_TRUE(targets.Contains(300));
    EXPECT_FALSE(targets.Contains(400));
}

TEST(TargetSetTest, DuplicateAddIsHarmless)
{
    TargetSet targets;
    targets.Add(100);
    targets.Add(100);
    targets.Add(100);

    EXPECT_TRUE(targets.Contains(100));

    targets.Remove(100);
    EXPECT_FALSE(targets.Contains(100));
}

//  Remove

TEST(TargetSetTest, RemoveDeletesOnlyThatPid)
{
    TargetSet targets;
    targets.Add(100);
    targets.Add(200);

    targets.Remove(100);

    EXPECT_FALSE(targets.Contains(100));
    EXPECT_TRUE(targets.Contains(200));
}

TEST(TargetSetTest, RemoveOnMissingPidDoesNothing)
{
    TargetSet targets;
    targets.Add(100);

    targets.Remove(999);

    EXPECT_TRUE(targets.Contains(100));
}

//  Clear All

TEST(TargetSetTest, ClearRemovesEverything)
{
    TargetSet targets;
    targets.Add(100);
    targets.Add(200);
    targets.Add(300);

    targets.Clear();

    EXPECT_FALSE(targets.Contains(100));
    EXPECT_FALSE(targets.Contains(200));
    EXPECT_FALSE(targets.Contains(300));
}

//  SetSingle

TEST(TargetSetTest, SetSingleReplacesExistingPids)
{
    TargetSet targets;
    targets.Add(100);
    targets.Add(200);

    targets.SetSingle(300);

    EXPECT_FALSE(targets.Contains(100));
    EXPECT_FALSE(targets.Contains(200));
    EXPECT_TRUE(targets.Contains(300));
}

TEST(TargetSetTest, SetSingleOnEmptySetAddsOne)
{
    TargetSet targets;

    targets.SetSingle(42);

    EXPECT_TRUE(targets.Contains(42));
}

TEST(TargetSetTest, SetSingleThenSetSingleReplaces)
{
    TargetSet targets;
    targets.SetSingle(100);
    targets.SetSingle(200);

    EXPECT_FALSE(targets.Contains(100));
    EXPECT_TRUE(targets.Contains(200));
}

//  Thread safety

TEST(TargetSetTest, ConcurrentAddsAndContains)
{
    TargetSet targets;
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 1000;

    std::vector<std::jthread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&targets, i]
        {
            for (int j = 0; j < kOpsPerThread; ++j)
            {
                auto pid = static_cast<uint32_t>((i * kOpsPerThread) + j);
                targets.Add(pid);
                targets.Contains(pid);
            }
        });
    }

    threads.clear();  // joins all

    EXPECT_TRUE(targets.Contains(0));
    EXPECT_TRUE(targets.Contains((kThreads * kOpsPerThread) - 1));
}
