#include "core/EffectConfig.h"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

using scrambler::core::TargetPidSet;

// Basic operations

TEST(TargetPidSetTest, EmptySetContainsNothing)
{
    TargetPidSet target_pids;

    EXPECT_FALSE(target_pids.Contains(1));
    EXPECT_FALSE(target_pids.Contains(0));
    EXPECT_FALSE(target_pids.Contains(99999));
}

TEST(TargetPidSetTest, AddedPidIsFound)
{
    TargetPidSet target_pids;
    target_pids.Add(1234);

    EXPECT_TRUE(target_pids.Contains(1234));
}

TEST(TargetPidSetTest, UnrelatedPidIsNotFound)
{
    TargetPidSet target_pids;
    target_pids.Add(1234);

    EXPECT_FALSE(target_pids.Contains(5678));
}

TEST(TargetPidSetTest, MultiplePidsCoexist)
{
    TargetPidSet target_pids;
    target_pids.Add(100);
    target_pids.Add(200);
    target_pids.Add(300);

    EXPECT_TRUE(target_pids.Contains(100));
    EXPECT_TRUE(target_pids.Contains(200));
    EXPECT_TRUE(target_pids.Contains(300));
    EXPECT_FALSE(target_pids.Contains(400));
}

TEST(TargetPidSetTest, DuplicateAddIsHarmless)
{
    TargetPidSet target_pids;
    target_pids.Add(100);
    target_pids.Add(100);
    target_pids.Add(100);

    EXPECT_TRUE(target_pids.Contains(100));

    target_pids.Remove(100);
    EXPECT_FALSE(target_pids.Contains(100));
}

//  Remove

TEST(TargetPidSetTest, RemoveDeletesOnlyThatPid)
{
    TargetPidSet target_pids;
    target_pids.Add(100);
    target_pids.Add(200);

    target_pids.Remove(100);

    EXPECT_FALSE(target_pids.Contains(100));
    EXPECT_TRUE(target_pids.Contains(200));
}

TEST(TargetPidSetTest, RemoveOnMissingPidDoesNothing)
{
    TargetPidSet target_pids;
    target_pids.Add(100);

    target_pids.Remove(999);

    EXPECT_TRUE(target_pids.Contains(100));
}

//  Clear All

TEST(TargetPidSetTest, ClearRemovesEverything)
{
    TargetPidSet target_pids;
    target_pids.Add(100);
    target_pids.Add(200);
    target_pids.Add(300);

    target_pids.Clear();

    EXPECT_FALSE(target_pids.Contains(100));
    EXPECT_FALSE(target_pids.Contains(200));
    EXPECT_FALSE(target_pids.Contains(300));
}

//  SetSelectedPid

TEST(TargetPidSetTest, SetSelectedPidReplacesExistingPids)
{
    TargetPidSet target_pids;
    target_pids.Add(100);
    target_pids.Add(200);

    target_pids.SetSelectedPid(300);

    EXPECT_FALSE(target_pids.Contains(100));
    EXPECT_FALSE(target_pids.Contains(200));
    EXPECT_TRUE(target_pids.Contains(300));
}

TEST(TargetPidSetTest, SetSelectedPidOnEmptySetAddsOne)
{
    TargetPidSet target_pids;

    target_pids.SetSelectedPid(42);

    EXPECT_TRUE(target_pids.Contains(42));
}

TEST(TargetPidSetTest, SetSelectedPidThenSetSelectedPidReplaces)
{
    TargetPidSet target_pids;
    target_pids.SetSelectedPid(100);
    target_pids.SetSelectedPid(200);

    EXPECT_FALSE(target_pids.Contains(100));
    EXPECT_TRUE(target_pids.Contains(200));
}

TEST(TargetPidSetTest, AddAfterSetSelectedPidKeepsBothPidsVisible)
{
    TargetPidSet target_pids;
    target_pids.SetSelectedPid(100);
    target_pids.Add(200);

    EXPECT_TRUE(target_pids.Contains(100));
    EXPECT_TRUE(target_pids.Contains(200));
}

TEST(TargetPidSetTest, RemoveFallsBackToRemainingSelectedPid)
{
    TargetPidSet target_pids;
    target_pids.Add(100);
    target_pids.Add(200);

    target_pids.Remove(100);

    EXPECT_FALSE(target_pids.Contains(100));
    EXPECT_TRUE(target_pids.Contains(200));
}

//  Thread safety

TEST(TargetPidSetTest, ConcurrentAddsAndContains)
{
    TargetPidSet target_pids;
    constexpr int kThreads = 8;
    constexpr int kOpsPerThread = 1000;

    std::vector<std::jthread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&target_pids, i]
        {
            for (int j = 0; j < kOpsPerThread; ++j)
            {
                auto pid = static_cast<uint32_t>((i * kOpsPerThread) + j);
                target_pids.Add(pid);
                target_pids.Contains(pid);
            }
        });
    }

    threads.clear();  // joins all

    EXPECT_TRUE(target_pids.Contains(0));
    EXPECT_TRUE(target_pids.Contains((kThreads * kOpsPerThread) - 1));
}
