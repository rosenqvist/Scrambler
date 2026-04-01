#include "platform/ProcessEnumerator.h"

#include <gtest/gtest.h>

using scrambler::platform::StartsWithInsensitive;
using scrambler::platform::IsSystemProcessPath;

TEST(StartsWithInsensitiveTest, BasicMatch)
{
    EXPECT_TRUE(StartsWithInsensitive(L"C:\\Windows\\System32\\foo.exe", L"C:\\Windows"));
}

TEST(StartsWithInsensitiveTest, CaseInsensitive)
{
    EXPECT_TRUE(StartsWithInsensitive(L"c:\\WINDOWS\\system32", L"C:\\Windows"));
}

TEST(StartsWithInsensitiveTest, NoMatch)
{
    EXPECT_FALSE(StartsWithInsensitive(L"D:\\Games\\foo.exe", L"C:\\Windows"));
}

TEST(StartsWithInsensitiveTest, PrefixLongerThanString)
{
    EXPECT_FALSE(StartsWithInsensitive(L"C:\\", L"C:\\Windows\\System32"));
}

TEST(StartsWithInsensitiveTest, EmptyStrings)
{
    EXPECT_TRUE(StartsWithInsensitive(L"anything", L""));
    EXPECT_FALSE(StartsWithInsensitive(L"", L"prefix"));
}

TEST(IsSystemProcessPathTest, EmptyPathTreatedAsSystem)
{
    EXPECT_TRUE(IsSystemProcessPath(L""));
}

TEST(IsSystemProcessPathTest, UserPathNotSystem)
{
    EXPECT_FALSE(IsSystemProcessPath(L"D:\\Games\\cs2.exe"));
}
