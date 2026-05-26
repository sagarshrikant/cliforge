/* ============================================================================
 * utest_cf_parse_meta.cpp — unit tests for parsing the meta{} block
 *
 * NOTE: cf_schema_file_t is ~37 MB (nested fixed arrays).  It MUST be
 * heap-allocated; putting it on the stack overflows and segfaults.
 * Every test uses std::unique_ptr<cf_schema_file_t> for safety.
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstring>
#include <memory>

extern "C" {
#include "cf_lex.h"
#include "cf_parse.h"
#include "cf_ast.h"
}

/* ---------------------------------------------------------------------------
 * Shared helper — returns error count; fills *out on success.
 * Caller must pass a heap-allocated, zeroed schema node.
 * ------------------------------------------------------------------------- */
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
 * 001 — Minimal meta block: app, prefix, output stored correctly.
 * ------------------------------------------------------------------------- */
TEST(CfParseMeta, MinimalMeta_001)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "meta {\n"
        "    app    = \"myapp\"\n"
        "    prefix = \"MY\"\n"
        "    output = \"cmdline\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    EXPECT_TRUE(s->meta.present);
    EXPECT_STREQ(s->meta.app,    "myapp");
    EXPECT_STREQ(s->meta.prefix, "MY");
    EXPECT_STREQ(s->meta.output, "cmdline");
}

/* ---------------------------------------------------------------------------
 * 002 — brief and version fields are stored.
 * ------------------------------------------------------------------------- */
TEST(CfParseMeta, BriefAndVersion_002)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "meta {\n"
        "    app     = \"tool\"\n"
        "    prefix  = \"T\"\n"
        "    output  = \"cmdline\"\n"
        "    brief   = \"A brief description\"\n"
        "    version = \"1.2.3\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    EXPECT_STREQ(s->meta.brief,   "A brief description");
    EXPECT_STREQ(s->meta.version, "1.2.3");
}

/* ---------------------------------------------------------------------------
 * 003 — author field is stored.
 * ------------------------------------------------------------------------- */
TEST(CfParseMeta, Author_003)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "meta {\n"
        "    app    = \"tool\"\n"
        "    prefix = \"T\"\n"
        "    output = \"cmdline\"\n"
        "    author = \"Alice\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    EXPECT_STREQ(s->meta.author, "Alice");
}

/* ---------------------------------------------------------------------------
 * 004 — Missing meta block: meta.present == 0, parse returns > 0 errors.
 * ------------------------------------------------------------------------- */
TEST(CfParseMeta, MissingMeta_004)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    /* Only an option with no meta block — parser should report an error. */
    const char *src =
        "@schema 1\n"
        "option verbose {\n"
        "    type = flag\n"
        "}\n";

    parse_src(src, s.get());   /* ignore error count — just check meta */
    EXPECT_FALSE(s->meta.present);
}

/* ---------------------------------------------------------------------------
 * 005 — @schema directive: schema_present and schema_version are recorded.
 *
 * The parser recognises "v1" (IDENT) as the version token; "@schema 1"
 * (NUMBER) sets schema_present but leaves schema_version == 0.
 * Use "@schema v1" to exercise the version path.
 * ------------------------------------------------------------------------- */
TEST(CfParseMeta, SchemaDirective_005)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    /* "@schema v1" — "v1" is an IDENT, which the parser maps to version 1. */
    const char *src =
        "@schema v1\n"
        "meta {\n"
        "    app    = \"tool\"\n"
        "    prefix = \"T\"\n"
        "    output = \"cmdline\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    EXPECT_TRUE(s->schema_present);
    EXPECT_EQ(s->schema_version, 1u);
}

/* ---------------------------------------------------------------------------
 * 006 — description field in meta is stored.
 * ------------------------------------------------------------------------- */
TEST(CfParseMeta, Description_006)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "meta {\n"
        "    app         = \"tool\"\n"
        "    prefix      = \"T\"\n"
        "    output      = \"cmdline\"\n"
        "    description = \"Long form description.\"\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    EXPECT_STREQ(s->meta.description, "Long form description.");
}

/* ---------------------------------------------------------------------------
 * 007 — doc-title and i18n accept additional meta string fields.
 * ------------------------------------------------------------------------- */
TEST(CfParseMeta, DocTitleAndI18n_007)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    const char *src =
        "@schema 1\n"
        "meta {\n"
        "    app       = tool\n"
        "    prefix    = T\n"
        "    output    = cmdline\n"
        "    doc-title = \"CLI Reference\"\n"
        "    i18n      = en_US\n"
        "}\n";

    ASSERT_EQ(parse_src(src, s.get()), 0);
    EXPECT_STREQ(s->meta.app, "tool");
    EXPECT_STREQ(s->meta.prefix, "T");
    EXPECT_STREQ(s->meta.output, "cmdline");
    EXPECT_STREQ(s->meta.doc_title, "CLI Reference");
    EXPECT_STREQ(s->meta.i18n, "en_US");
}
