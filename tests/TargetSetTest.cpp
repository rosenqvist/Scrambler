#include "core/EffectConfig.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace scrambler::core
{

TEST(TargetSet, EmptySetContainsNothing)
{
    TargetSet targets;

    EXPECT_FALSE(targets.Contains(1));
    EXPECT_FALSE(targets.Contains(0));
    EXPECT_FALSE(targets.Contains(99999));
}

TEST(TargetSet, AddedPidIsFound)
{
    TargetSet targets;
    targets.Add(1234);

    EXPECT_TRUE(targets.Contains(1234));
}

TEST(TargetSet, UnrelatedPidIsNotFound)
{
    TargetSet targets;
    targets.Add(1234);

    EXPECT_FALSE(targets.Contains(5678));
}

TEST(TargetSet, MultiplePidsCoexist)
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

TEST(TargetSet, RemoveDeletesOnlyThatPid)
{
    TargetSet targets;
    targets.Add(100);
    targets.Add(200);

    targets.Remove(100);

    EXPECT_FALSE(targets.Contains(100));
    EXPECT_TRUE(targets.Contains(200));
}

TEST(TargetSet, RemoveOnMissingPidDoesNothing)
{
    TargetSet targets;
    targets.Add(100);

    targets.Remove(999);

    EXPECT_TRUE(targets.Contains(100));
}

TEST(TargetSet, ClearRemovesEverything)
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

TEST(TargetSet, DuplicateAddIsHarmless)
{
    TargetSet targets;
    targets.Add(100);
    targets.Add(100);
    targets.Add(100);

    EXPECT_TRUE(targets.Contains(100));

    targets.Remove(100);
    EXPECT_FALSE(targets.Contains(100));
}

TEST(TargetSet, ConcurrentAddsAndContains)
{
    TargetSet targets;
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 1000;

    // check for data races.
    // also run under TSan to catch anything not visible here
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

    // Spot check a few values made it in
    EXPECT_TRUE(targets.Contains(0));
    EXPECT_TRUE(targets.Contains((kThreads * kOpsPerThread) - 1));
}

}  // namespace scrambler::core
