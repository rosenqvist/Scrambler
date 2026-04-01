#include "core/Types.h"

#include <gtest/gtest.h>
#include <unordered_set>

using scrambler::core::FiveTuple;
using scrambler::core::FiveTupleHash;

// Equality

TEST(FiveTupleTest, EqualTuplesCompareEqual)
{
    FiveTuple a{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};
    FiveTuple b{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};

    EXPECT_EQ(a, b);
}

TEST(FiveTupleTest, DifferentFieldsMakeUnequal)
{
    FiveTuple base{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};

    FiveTuple diff_src_addr = base;
    diff_src_addr.src_addr = 999;
    EXPECT_NE(base, diff_src_addr);

    FiveTuple diff_dst_port = base;
    diff_dst_port.dst_port = 9999;
    EXPECT_NE(base, diff_dst_port);

    FiveTuple diff_protocol = base;
    diff_protocol.protocol = 6;
    EXPECT_NE(base, diff_protocol);
}

//  Reversed Tuple

TEST(FiveTupleTest, ReversedSwapsSrcAndDst)
{
    FiveTuple original{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};
    auto reversed = original.Reversed();

    EXPECT_EQ(reversed.src_addr, original.dst_addr);
    EXPECT_EQ(reversed.dst_addr, original.src_addr);
    EXPECT_EQ(reversed.src_port, original.dst_port);
    EXPECT_EQ(reversed.dst_port, original.src_port);
    EXPECT_EQ(reversed.protocol, original.protocol);
}

TEST(FiveTupleTest, ReversedRoundTripGivesOriginal)
{
    FiveTuple original{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};

    EXPECT_EQ(original.Reversed().Reversed(), original);
}

TEST(FiveTupleTest, ReversedIsNotEqualToOriginal)
{
    FiveTuple original{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};

    EXPECT_NE(original, original.Reversed());
}

TEST(FiveTupleTest, SymmetricTupleReversesToItself)
{
    FiveTuple symmetric{.src_addr = 100, .dst_addr = 100, .src_port = 3000, .dst_port = 3000, .protocol = 17};

    EXPECT_EQ(symmetric, symmetric.Reversed());
}

//  Hash Quality

TEST(FiveTupleTest, EqualTuplesProduceSameHash)
{
    FiveTuple a{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};
    FiveTuple b{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};

    FiveTupleHash hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST(FiveTupleTest, SingleFieldDifferencesProduceDifferentHashes)
{
    FiveTupleHash hasher;

    FiveTuple base{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};

    FiveTuple diff_src = base;
    diff_src.src_addr = 999;

    FiveTuple diff_dst = base;
    diff_dst.dst_port = 4001;

    FiveTuple diff_proto = base;
    diff_proto.protocol = 6;

    EXPECT_NE(hasher(base), hasher(diff_src));
    EXPECT_NE(hasher(base), hasher(diff_dst));
    EXPECT_NE(hasher(base), hasher(diff_proto));
    EXPECT_NE(hasher(diff_src), hasher(diff_dst));
}

TEST(FiveTupleTest, ReversedTupleHashesDifferently)
{
    FiveTupleHash hasher;
    FiveTuple t{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};

    EXPECT_NE(hasher(t), hasher(t.Reversed()));
}

TEST(FiveTupleTest, NearbyPortsDontCollide)
{
    FiveTupleHash hasher;
    std::unordered_set<std::size_t> hashes;

    for (uint16_t port = 27000; port < 27050; ++port)
    {
        FiveTuple t{.src_addr = 100, .dst_addr = 200, .src_port = port, .dst_port = 4000, .protocol = 17};
        hashes.insert(hasher(t));
    }

    EXPECT_EQ(hashes.size(), 50);
}

TEST(FiveTupleTest, WorksAsUnorderedMapKey)
{
    std::unordered_map<FiveTuple, int, FiveTupleHash> map;

    FiveTuple a{.src_addr = 100, .dst_addr = 200, .src_port = 3000, .dst_port = 4000, .protocol = 17};
    FiveTuple b{.src_addr = 300, .dst_addr = 400, .src_port = 5000, .dst_port = 6000, .protocol = 17};

    map[a] = 1;
    map[b] = 2;

    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map[a], 1);
    EXPECT_EQ(map[b], 2);
}
