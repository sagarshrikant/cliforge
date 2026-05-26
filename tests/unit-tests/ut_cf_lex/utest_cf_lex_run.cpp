/* ============================================================================
 * utest_cf_lex_run.cpp — unit tests for cf_lex_init() / cf_lex_run()
 *
 * Each test drives the lexer on a small source snippet and inspects the
 * resulting token array.  We check token types, text content, and source
 * location (line/col) where relevant.
 *
 * Helper: lex_src(src) — initialises and runs the lexer, ASSERTs on error.
 * ========================================================================= */
#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "cf_lex.h"
}

/* ---------------------------------------------------------------------------
 * Test fixture: owns the lexer state so each TEST has a clean slate.
 * ------------------------------------------------------------------------- */
class CfLexTest : public ::testing::Test
{
protected:
    cf_lexer_t lex{};

    /* Run the lexer on a NUL-terminated string.  Returns 0 on success. */
    int lex_src(const char *src)
    {
        cf_lex_init(&lex, src,
                    static_cast<unsigned int>(std::strlen(src)),
                    "<test>");
        return cf_lex_run(&lex);
    }

    /* Return token at index i (bounds-checked). */
    const cf_token_t &tok(unsigned int i) const
    {
        EXPECT_LT(i, lex.ntokens);
        return lex.tokens[i];
    }

    /* Compare token text to expected string without NUL-terminating. */
    bool tok_text_eq(unsigned int i, const char *expected) const
    {
        const cf_token_t &t = lex.tokens[i];
        std::size_t elen = std::strlen(expected);
        if (t.len != static_cast<unsigned int>(elen))
            return false;
        return std::memcmp(t.start, expected, elen) == 0;
    }
};

/* ---------------------------------------------------------------------------
 * 001 — Empty source produces exactly one EOF token.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, EmptySource_001)
{
    ASSERT_EQ(lex_src(""), 0);
    ASSERT_GE(lex.ntokens, 1u);
    EXPECT_EQ(tok(0).type, CF_TOK_EOF);
}

/* ---------------------------------------------------------------------------
 * 002 — Simple identifier is emitted as CF_TOK_IDENT.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, SimpleIdent_002)
{
    ASSERT_EQ(lex_src("meta"), 0);
    /* tokens: IDENT("meta"), EOF */
    ASSERT_GE(lex.ntokens, 2u);
    EXPECT_EQ(tok(0).type, CF_TOK_IDENT);
    EXPECT_TRUE(tok_text_eq(0, "meta"));
    EXPECT_EQ(tok(0).line, 1u);
    EXPECT_EQ(tok(0).col,  1u);
}

/* ---------------------------------------------------------------------------
 * 003 — Hyphenated identifier is a single IDENT token.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, HyphenatedIdent_003)
{
    ASSERT_EQ(lex_src("doc-title"), 0);
    ASSERT_GE(lex.ntokens, 2u);
    EXPECT_EQ(tok(0).type, CF_TOK_IDENT);
    EXPECT_TRUE(tok_text_eq(0, "doc-title"));
}

/* ---------------------------------------------------------------------------
 * 004 — Integer number literal.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, NumberLiteral_004)
{
    ASSERT_EQ(lex_src("42"), 0);
    ASSERT_GE(lex.ntokens, 2u);
    EXPECT_EQ(tok(0).type, CF_TOK_NUMBER);
    EXPECT_TRUE(tok_text_eq(0, "42"));
}

/* ---------------------------------------------------------------------------
 * 005 — Quantity literal (number + unit suffix) is a single QUANTITY token.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, QuantityLiteral_005)
{
    ASSERT_EQ(lex_src("4MiB"), 0);
    ASSERT_GE(lex.ntokens, 2u);
    EXPECT_EQ(tok(0).type, CF_TOK_QUANTITY);
    EXPECT_TRUE(tok_text_eq(0, "4MiB"));
}

/* ---------------------------------------------------------------------------
 * 006 — Quoted string literal.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, StringLiteral_006)
{
    ASSERT_EQ(lex_src("\"hello world\""), 0);
    ASSERT_GE(lex.ntokens, 2u);
    EXPECT_EQ(tok(0).type, CF_TOK_STRING);
    EXPECT_TRUE(tok_text_eq(0, "\"hello world\""));
}

/* ---------------------------------------------------------------------------
 * 007 — @import directive token.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, DirectiveImport_007)
{
    ASSERT_EQ(lex_src("@import"), 0);
    ASSERT_GE(lex.ntokens, 2u);
    EXPECT_EQ(tok(0).type, CF_TOK_DIRECTIVE);
    EXPECT_TRUE(tok_text_eq(0, "@import"));
}

/* ---------------------------------------------------------------------------
 * 008 — Doc-comment (triple-slash) produces CF_TOK_DOC_COMMENT.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, DocComment_008)
{
    ASSERT_EQ(lex_src("/// This is doc\n"), 0);
    /* first non-whitespace token is a doc comment */
    ASSERT_GE(lex.ntokens, 2u);
    EXPECT_EQ(tok(0).type, CF_TOK_DOC_COMMENT);
}

/* ---------------------------------------------------------------------------
 * 009 — Punctuation: braces, brackets, operators.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, Punctuation_009)
{
    ASSERT_EQ(lex_src("{ } [ ] = =="), 0);
    /* expected: LBRACE RBRACE LBRACKET RBRACKET ASSIGN EQ EOF */
    ASSERT_GE(lex.ntokens, 7u);
    EXPECT_EQ(tok(0).type, CF_TOK_LBRACE);
    EXPECT_EQ(tok(1).type, CF_TOK_RBRACE);
    EXPECT_EQ(tok(2).type, CF_TOK_LBRACKET);
    EXPECT_EQ(tok(3).type, CF_TOK_RBRACKET);
    EXPECT_EQ(tok(4).type, CF_TOK_ASSIGN);
    EXPECT_EQ(tok(5).type, CF_TOK_EQ);
}

/* ---------------------------------------------------------------------------
 * 010 — Line number advances correctly across newlines.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, LineNumbers_010)
{
    const char *src = "a\nb\nc\n";
    ASSERT_EQ(lex_src(src), 0);
    /* Three IDENT tokens on lines 1, 2, 3 */
    ASSERT_GE(lex.ntokens, 4u);
    EXPECT_EQ(tok(0).line, 1u);
    EXPECT_EQ(tok(1).line, 2u);
    EXPECT_EQ(tok(2).line, 3u);
}

/* ---------------------------------------------------------------------------
 * 011 — Column numbers are correct within a line.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, ColumnNumbers_011)
{
    const char *src = "  foo  bar";
    ASSERT_EQ(lex_src(src), 0);
    ASSERT_GE(lex.ntokens, 3u);
    EXPECT_EQ(tok(0).col, 3u); /* "foo" starts at column 3 */
    EXPECT_EQ(tok(1).col, 8u); /* "bar" starts at column 8 */
}

/* ---------------------------------------------------------------------------
 * 012 — Ordinary // comment is skipped (CF_TOK_COMMENT or not emitted).
 *       Either way the next meaningful token must be the ident after it.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, LineComment_012)
{
    const char *src = "// skip this\nvalue";
    ASSERT_EQ(lex_src(src), 0);
    ASSERT_GE(lex.ntokens, 2u);
    /* Find first non-comment, non-EOF token */
    unsigned int i = 0;
    while (i < lex.ntokens &&
           (lex.tokens[i].type == CF_TOK_COMMENT ||
            lex.tokens[i].type == CF_TOK_DOC_COMMENT))
        i++;
    ASSERT_LT(i, lex.ntokens);
    EXPECT_EQ(lex.tokens[i].type, CF_TOK_IDENT);
    EXPECT_TRUE(tok_text_eq(i, "value"));
}

/* ---------------------------------------------------------------------------
 * 013 — cf_tok_str() returns a non-empty string for every valid token type.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, TokStr_013)
{
    for (int t = CF_TOK_IDENT; t <= CF_TOK_INVALID; t++) {
        const char *name = cf_tok_str(static_cast<cf_tok_type_t>(t));
        EXPECT_NE(name, nullptr)       << "cf_tok_str(" << t << ") is NULL";
        EXPECT_GT(std::strlen(name), 0u) << "cf_tok_str(" << t << ") is empty";
    }
}

/* ---------------------------------------------------------------------------
 * 014 — cf_tok_text() copies token text, NUL-terminates, truncates safely.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, TokText_014)
{
    ASSERT_EQ(lex_src("hello"), 0);
    ASSERT_GE(lex.ntokens, 2u);

    char buf[8] = {};
    cf_tok_text(&lex.tokens[0], buf, sizeof(buf));
    EXPECT_STREQ(buf, "hello");

    /* Truncation: buffer smaller than token */
    char small[3] = {};
    cf_tok_text(&lex.tokens[0], small, sizeof(small));
    EXPECT_EQ(small[2], '\0'); /* always NUL-terminated */
    EXPECT_EQ(small[0], 'h');
    EXPECT_EQ(small[1], 'e');
}

/* ---------------------------------------------------------------------------
 * 015 — Minimal schema snippet tokenises without error.
 * ------------------------------------------------------------------------- */
TEST_F(CfLexTest, MinimalSchema_015)
{
    const char *src =
        "@schema 1\n"
        "meta {\n"
        "    app = \"myapp\"\n"
        "    prefix = \"MY\"\n"
        "    output = \"cmdline\"\n"
        "}\n";
    ASSERT_EQ(lex_src(src), 0);
    /* Must produce at least: @schema, 1, meta, {, app, =, "myapp", ..., } */
    EXPECT_GT(lex.ntokens, 8u);
    EXPECT_EQ(tok(0).type, CF_TOK_DIRECTIVE);
    EXPECT_TRUE(tok_text_eq(0, "@schema"));
}
