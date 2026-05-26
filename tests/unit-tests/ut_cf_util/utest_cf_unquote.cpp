/* ============================================================================
 * utest_cf_unquote.cpp — unit tests for cf_unquote()
 *
 * cf_unquote(dst, dstsz, src, src_len):
 *   src is a raw string token including surrounding double-quotes.
 *   Strips quotes, resolves \n \t \\ \" escape sequences,
 *   and handles backslash-newline continuation.
 *   Returns 0 on success, -1 if dst too small.
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "cf_util.h"
}

/* Helper: wrap a C string literal in quotes and call cf_unquote. */
static int unquote(char *dst, unsigned int dstsz, const char *raw)
{
    return cf_unquote(dst, dstsz, raw, (unsigned int)strlen(raw));
}

/* Simple string with no escapes. */
TEST(CfUnquote, Simple_001)
{
    char dst[32] = {};
    int rc = unquote(dst, sizeof(dst), "\"hello\"");
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(dst, "hello");
}

/* Empty string literal. */
TEST(CfUnquote, EmptyString_002)
{
    char dst[8] = {};
    int rc = unquote(dst, sizeof(dst), "\"\"");
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(dst, "");
}

/* Backslash-n escape becomes newline. */
TEST(CfUnquote, EscapeNewline_003)
{
    char dst[16] = {};
    int rc = unquote(dst, sizeof(dst), "\"a\\nb\"");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(dst[0], 'a');
    EXPECT_EQ(dst[1], '\n');
    EXPECT_EQ(dst[2], 'b');
}

/* Backslash-t escape becomes tab. */
TEST(CfUnquote, EscapeTab_004)
{
    char dst[16] = {};
    int rc = unquote(dst, sizeof(dst), "\"a\\tb\"");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(dst[1], '\t');
}

/* Escaped double-quote inside string. */
TEST(CfUnquote, EscapedQuote_005)
{
    char dst[16] = {};
    int rc = unquote(dst, sizeof(dst), "\"say \\\"hi\\\"\"");
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(dst, "say \"hi\"");
}

/* Escaped backslash becomes single backslash. */
TEST(CfUnquote, EscapedBackslash_006)
{
    char dst[16] = {};
    int rc = unquote(dst, sizeof(dst), "\"a\\\\b\"");
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(dst, "a\\b");
}

/* Destination too small — must return -1. */
TEST(CfUnquote, DstTooSmall_007)
{
    char dst[3] = {};
    int rc = unquote(dst, sizeof(dst), "\"hello world\"");
    EXPECT_EQ(rc, -1);
}
