/* ============================================================================
 * utest_cf_ident_to_c.cpp — unit tests for cf_ident_to_c()
 *
 * cf_ident_to_c(dst, src, dstsz):
 *   Copies src into dst replacing every '-' with '_'.
 *   Result is a valid C identifier.
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "cf_util.h"
}

/* No hyphens — output equals input. */
TEST(CfIdentToC, NoHyphens_001)
{
    char dst[32] = {};
    cf_ident_to_c(dst, "verbose", sizeof(dst));
    EXPECT_STREQ(dst, "verbose");
}

/* Single hyphen replaced. */
TEST(CfIdentToC, SingleHyphen_002)
{
    char dst[32] = {};
    cf_ident_to_c(dst, "div-zero", sizeof(dst));
    EXPECT_STREQ(dst, "div_zero");
}

/* Multiple hyphens all replaced. */
TEST(CfIdentToC, MultipleHyphens_003)
{
    char dst[32] = {};
    cf_ident_to_c(dst, "log-level-max", sizeof(dst));
    EXPECT_STREQ(dst, "log_level_max");
}

/* Leading hyphen (edge case). */
TEST(CfIdentToC, LeadingHyphen_004)
{
    char dst[32] = {};
    cf_ident_to_c(dst, "-flag", sizeof(dst));
    EXPECT_STREQ(dst, "_flag");
}

/* Truncation when src longer than dstsz-1. */
TEST(CfIdentToC, Truncation_005)
{
    char dst[5] = {};
    cf_ident_to_c(dst, "div-zero-policy", sizeof(dst));
    EXPECT_STREQ(dst, "div_");  /* 4 chars + NUL */
}
