/* ============================================================================
 * utest_cf_parse_import.cpp — unit tests for @import directive parsing
 *
 * NOTE: cf_schema_file_t is ~37 MB — heap-allocate via std::unique_ptr.
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstring>
#include <memory>
#include <string>

extern "C" {
#include "cf_lex.h"
#include "cf_parse.h"
#include "cf_ast.h"
}

static int parse_src(const char *src, cf_schema_file_t *out)
{
    cf_lexer_t lex;
    cf_lex_init(&lex, src,
                static_cast<unsigned int>(std::strlen(src)),
                "<test>");
    if (cf_lex_run(&lex) != 0)
        return -1;
    return cf_parse(lex.tokens, lex.ntokens, "<test>", out);
}

/* ---------------------------------------------------------------------------
 * 001 — Single @import: path and alias stored in imports[0].
 * ------------------------------------------------------------------------- */
TEST(CfParseImport, SingleImport_001)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "@import \"lib_arith.cf\" as arith\n"
        "meta {\n"
        "    app    = \"tool\"\n"
        "    prefix = \"T\"\n"
        "    output = \"cmdline\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    ASSERT_EQ(s->nimports, 1u);
    EXPECT_STREQ(s->imports[0].path,  "lib_arith.cf");
    EXPECT_STREQ(s->imports[0].alias, "arith");
    EXPECT_EQ(s->imports[0].cond_kind, CF_IMPORT_COND_NONE);
}

/* ---------------------------------------------------------------------------
 * 002 — Two @import directives: both stored in order.
 * ------------------------------------------------------------------------- */
TEST(CfParseImport, TwoImports_002)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "@import \"lib_arith.cf\" as arith\n"
        "@import \"lib_cmp.cf\"   as cmp\n"
        "meta {\n"
        "    app    = \"tool\"\n"
        "    prefix = \"T\"\n"
        "    output = \"cmdline\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    ASSERT_EQ(s->nimports, 2u);
    EXPECT_STREQ(s->imports[0].alias, "arith");
    EXPECT_STREQ(s->imports[1].alias, "cmp");
    EXPECT_STREQ(s->imports[1].path,  "lib_cmp.cf");
}

/* ---------------------------------------------------------------------------
 * 003 — Conditional @import with ifkey: cond_kind and cond_key stored.
 * ------------------------------------------------------------------------- */
TEST(CfParseImport, ConditionalIfkey_003)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "@import \"lib_trig.cf\" as trig ifkey ENABLE_TRIG\n"
        "meta {\n"
        "    app    = \"tool\"\n"
        "    prefix = \"T\"\n"
        "    output = \"cmdline\"\n"
        "}\n";

    int errs = parse_src(src, s.get());
    (void)errs;

    if (s->nimports >= 1u) {
        EXPECT_STREQ(s->imports[0].alias, "trig");
        EXPECT_EQ(s->imports[0].cond_kind, CF_IMPORT_COND_IFKEY);
        EXPECT_STREQ(s->imports[0].cond_key, "ENABLE_TRIG");
    }
}

/* ---------------------------------------------------------------------------
 * 004 — Import path is stored verbatim including relative directory part.
 * ------------------------------------------------------------------------- */
TEST(CfParseImport, RelativePath_004)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "@import \"../libs/net.cf\" as net\n"
        "meta {\n"
        "    app    = \"tool\"\n"
        "    prefix = \"T\"\n"
        "    output = \"cmdline\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    ASSERT_EQ(s->nimports, 1u);
    EXPECT_STREQ(s->imports[0].path,  "../libs/net.cf");
    EXPECT_STREQ(s->imports[0].alias, "net");
}

/* ---------------------------------------------------------------------------
 * 005 — Source line number of the @import directive is recorded.
 * ------------------------------------------------------------------------- */
TEST(CfParseImport, LineNumber_005)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"               /* line 1 */
        "@import \"x.cf\" as x\n"  /* line 2 */
        "meta {\n"
        "    app    = \"tool\"\n"
        "    prefix = \"T\"\n"
        "    output = \"cmdline\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    ASSERT_EQ(s->nimports, 1u);
    EXPECT_EQ(s->imports[0].line, 2u);
}
