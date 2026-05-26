/* ============================================================================
 * utest_cf_str_eq_tok.cpp — unit tests for cf_str_eq_tok()
 *
 * cf_str_eq_tok(s, tok, tok_len):
 *   Returns non-zero if NUL-terminated s equals the first tok_len bytes
 *   of tok (tok is NOT NUL-terminated, it's a token slice).
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "cf_util.h"
}

TEST(CfStrEqTok, Match_001)
{
    const char *buf = "prefix hello suffix";
    /* token "hello" starts at offset 7, length 5 */
    EXPECT_NE(cf_str_eq_tok("hello", buf + 7, 5), 0);
}

TEST(CfStrEqTok, NoMatch_002)
{
    const char *buf = "world";
    EXPECT_EQ(cf_str_eq_tok("hello", buf, 5), 0);
}

/* Length mismatch: tok_len shorter than s — should not match. */
TEST(CfStrEqTok, LengthMismatch_003)
{
    const char *buf = "hello_extra";
    EXPECT_EQ(cf_str_eq_tok("hello", buf, 3), 0); /* "hel" != "hello" */
}

/* Empty token matches empty string. */
TEST(CfStrEqTok, BothEmpty_004)
{
    EXPECT_NE(cf_str_eq_tok("", "", 0), 0);
}
