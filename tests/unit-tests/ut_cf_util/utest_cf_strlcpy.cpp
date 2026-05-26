/* ============================================================================
 * utest_cf_strlcpy.cpp — unit tests for cf_strlcpy()
 *
 * cf_strlcpy(dst, src, dstsz):
 *   Copies at most dstsz-1 bytes of src into dst, always NUL-terminates dst.
 *   Returns number of bytes written (not counting NUL).
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "cf_util.h"
}

/* Normal copy — src fits entirely in dst. */
TEST(CfStrlcpy, NormalCopy_001)
{
    char dst[16] = {};
    unsigned int n = cf_strlcpy(dst, "hello", sizeof(dst));
    EXPECT_STREQ(dst, "hello");
    EXPECT_EQ(n, 5u);
}

/* Exact fit — src length == dstsz-1, NUL must be written. */
TEST(CfStrlcpy, ExactFit_002)
{
    char dst[6] = {};
    unsigned int n = cf_strlcpy(dst, "hello", sizeof(dst)); /* 5 chars + NUL = 6 */
    EXPECT_STREQ(dst, "hello");
    EXPECT_EQ(n, 5u);
    EXPECT_EQ(dst[5], '\0');
}

/* Truncation — src longer than dstsz-1. */
TEST(CfStrlcpy, Truncation_003)
{
    char dst[4] = {};
    unsigned int n = cf_strlcpy(dst, "hello", sizeof(dst)); /* room for 3 + NUL */
    EXPECT_STREQ(dst, "hel");
    EXPECT_EQ(n, 3u);
    EXPECT_EQ(dst[3], '\0');
}

/* Empty source — dst must be NUL-terminated, return 0. */
TEST(CfStrlcpy, EmptySource_004)
{
    char dst[8] = {'x','x','x','x','x','x','x','x'};
    unsigned int n = cf_strlcpy(dst, "", sizeof(dst));
    EXPECT_EQ(dst[0], '\0');
    EXPECT_EQ(n, 0u);
}

/* Single-byte destination — only NUL can fit. */
TEST(CfStrlcpy, OneByteBuffer_005)
{
    char dst[1] = {'x'};
    unsigned int n = cf_strlcpy(dst, "hello", sizeof(dst));
    EXPECT_EQ(dst[0], '\0');
    EXPECT_EQ(n, 0u);
}

/* Copy of a string containing embedded spaces and punctuation. */
TEST(CfStrlcpy, PunctuationAndSpaces_006)
{
    char dst[32] = {};
    unsigned int n = cf_strlcpy(dst, "hello, world!", sizeof(dst));
    EXPECT_STREQ(dst, "hello, world!");
    EXPECT_EQ(n, 13u);
}
