/* ============================================================================
 * utest_cf_parse_option.cpp — unit tests for option{} block parsing
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

/* Boilerplate meta header */
static const char k_header[] =
    "@schema 1\n"
    "meta {\n"
    "    app    = \"tool\"\n"
    "    prefix = \"T\"\n"
    "    output = \"cmdline\"\n"
    "}\n";

static int parse_src(const char *src, cf_schema_file_t *out)
{
    std::string full = std::string(k_header) + src;
    cf_lexer_t lex;
    cf_lex_init(&lex, full.c_str(),
                static_cast<unsigned int>(full.size()),
                "<test>");
    if (cf_lex_run(&lex) != 0)
        return -1;
    return cf_parse(lex.tokens, lex.ntokens, "<test>", out);
}

/* ---------------------------------------------------------------------------
 * 001 — FLAG type option.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, FlagType_001)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option verbose {\n"
        "    type = flag\n"
        "    help = \"Enable verbose output\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_STREQ(s->options[0].name, "verbose");
    EXPECT_EQ(s->options[0].type.base, CF_TYPE_FLAG);
    EXPECT_STREQ(s->options[0].help, "Enable verbose output");
}

/* ---------------------------------------------------------------------------
 * 002 — UINT32 type with default value.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, Uint32WithDefault_002)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option workers {\n"
        "    type    = uint32\n"
        "    default = 4\n"
        "    help    = \"Thread count\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].type.base, CF_TYPE_UINT32);
    EXPECT_TRUE(s->options[0].has_default);
    EXPECT_STREQ(s->options[0].default_val, "4");
}

/* ---------------------------------------------------------------------------
 * 003 — short option character is stored.
 *
 * The parser accepts CF_TOK_CHAR ('o') or CF_TOK_STRING ("o").
 * A bare identifier is silently skipped, so we use the char literal form.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, ShortOpt_003)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option output {\n"
        "    type  = string\n"
        "    short = 'o'\n"
        "    help  = \"Output path\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].short_opt, 'o');
}

/* ---------------------------------------------------------------------------
 * 004 — required = mandatory sets CF_REQ_MANDATORY.
 *
 * The parser keyword is "mandatory", not "true".
 * Any other value (including "true") maps to CF_REQ_OPTIONAL.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, Required_004)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option config {\n"
        "    type     = file\n"
        "    required = mandatory\n"
        "    help     = \"Config file\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].required, CF_REQ_MANDATORY);
}

/* ---------------------------------------------------------------------------
 * 005 — visible = detail sets CF_VIS_DETAIL.
 *
 * The parser keyword is "visible = detail" (shown under --help-detail only).
 * There is no "hidden" keyword; the schema SPEC uses visible = detail/never/all.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, HiddenDetail_005)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option debug-trace {\n"
        "    type    = flag\n"
        "    visible = detail\n"
        "    help    = \"Internal trace\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].visible, CF_VIS_DETAIL);
}

/* ---------------------------------------------------------------------------
 * 006 — visible = never sets CF_VIS_NEVER (never shown in any help variant).
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, StrictHidden_006)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option internal-key {\n"
        "    type    = string\n"
        "    visible = never\n"
        "    help    = \"Never shown\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].visible, CF_VIS_NEVER);
}

/* ---------------------------------------------------------------------------
 * 007 — STRING type, no default: has_default == 0.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, StringNoDefault_007)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option label {\n"
        "    type = string\n"
        "    help = \"A label\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].type.base, CF_TYPE_STRING);
    EXPECT_FALSE(s->options[0].has_default);
}

/* ---------------------------------------------------------------------------
 * 008 — BOOL type with default = false.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, BoolFalseDefault_008)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option dry-run {\n"
        "    type    = bool\n"
        "    default = false\n"
        "    help    = \"Dry run mode\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].type.base, CF_TYPE_BOOL);
    EXPECT_TRUE(s->options[0].has_default);
    EXPECT_STREQ(s->options[0].default_val, "false");
}

/* ---------------------------------------------------------------------------
 * 009 — Option inside section{}: stored in sections[0].options[].
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, OptionInsideSection_009)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "section \"Network\" {\n"
        "    option port {\n"
        "        type    = uint16\n"
        "        default = 8080\n"
        "        help    = \"Listen port\"\n"
        "    }\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->nsections, 1u);
    EXPECT_STREQ(s->sections[0].name, "Network");
    ASSERT_EQ(s->sections[0].noptions, 1u);
    EXPECT_STREQ(s->sections[0].options[0].name, "port");
    EXPECT_EQ(s->sections[0].options[0].type.base, CF_TYPE_UINT16);
}

/* ---------------------------------------------------------------------------
 * 010 — sensitive = true is stored.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, Sensitive_010)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option api-key {\n"
        "    type      = string\n"
        "    sensitive = true\n"
        "    help      = \"API key\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_TRUE(s->options[0].sensitive);
}

/* ---------------------------------------------------------------------------
 * 011 — ENUM (inline choice): members stored in type.members[].
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, InlineEnum_011)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option level {\n"
        "    type    = (debug, info, warn, error)\n"
        "    default = info\n"
        "    help    = \"Log level\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].type.base, CF_TYPE_CHOICE);
    EXPECT_EQ(s->options[0].type.nmembers, 4u);
    EXPECT_STREQ(s->options[0].type.members[0], "debug");
    EXPECT_STREQ(s->options[0].type.members[3], "error");
}

/* ---------------------------------------------------------------------------
 * 012 — Two top-level options: noptions == 2.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, TwoTopLevelOptions_012)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option alpha {\n"
        "    type = flag\n"
        "    help = \"Alpha\"\n"
        "}\n"
        "option beta {\n"
        "    type = flag\n"
        "    help = \"Beta\"\n"
        "}\n", s.get()), 0);

    EXPECT_EQ(s->noptions, 2u);
    EXPECT_STREQ(s->options[0].name, "alpha");
    EXPECT_STREQ(s->options[1].name, "beta");
}

/* ---------------------------------------------------------------------------
 * 013 — Rich option qualifiers populate the full option metadata.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, RichQualifiers_013)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option endpoint {\n"
        "    type         = string(length=32)\n"
        "    multiple     = 1..3\n"
        "    alias        = \"server-endpoint\"\n"
        "    depends-on   = mode\n"
        "    conflicts    = legacy\n"
        "    deprecated   = \"Use host instead\"\n"
        "    display-unit = ms\n"
        "    help         = \"Endpoint\"\n"
        "    details      = \"Extended endpoint help\"\n"
        "    note         = \"Internal note\"\n"
        "    example      = \"--endpoint api\"\n"
        "    since        = \"2.0\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].type.base, CF_TYPE_STRING);
    EXPECT_EQ(s->options[0].type.str_len, 32u);
    EXPECT_TRUE(s->options[0].multiple.enabled);
    EXPECT_EQ(s->options[0].multiple.min, 1u);
    EXPECT_EQ(s->options[0].multiple.max, 3u);
    EXPECT_STREQ(s->options[0].alias_name, "server-endpoint");
    EXPECT_STREQ(s->options[0].depends_on, "mode");
    EXPECT_STREQ(s->options[0].conflicts, "legacy");
    EXPECT_TRUE(s->options[0].is_deprecated);
    EXPECT_STREQ(s->options[0].deprecated, "Use host instead");
    EXPECT_STREQ(s->options[0].display_unit, "ms");
    EXPECT_STREQ(s->options[0].details, "Extended endpoint help");
    EXPECT_STREQ(s->options[0].note, "Internal note");
    EXPECT_STREQ(s->options[0].example, "--endpoint api");
    EXPECT_STREQ(s->options[0].since, "2.0");
}

/* ---------------------------------------------------------------------------
 * 014 — Ranges, variant defaults, and negative numeric defaults are parsed.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, VariantAndNegativeDefaults_014)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "option threshold {\n"
        "    type    = sint32\n"
        "    default = -5\n"
        "    help    = \"Threshold\"\n"
        "}\n"
        "option timeout {\n"
        "    type    = duration in 1ms..5s\n"
        "    default = { ifkey MODE == fast : 1ms, default : 2s }\n"
        "    help    = \"Timeout\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->noptions, 2u);
    EXPECT_STREQ(s->options[0].default_val, "-5");

    EXPECT_EQ(s->options[1].type.base, CF_TYPE_DURATION);
    EXPECT_TRUE(s->options[1].type.has_range);
    EXPECT_STREQ(s->options[1].type.range_lo, "1ms");
    EXPECT_STREQ(s->options[1].type.range_hi, "5s");
    EXPECT_TRUE(s->options[1].has_default);
    EXPECT_STREQ(s->options[1].default_val, "2s");
}

/* ---------------------------------------------------------------------------
 * 015 — Named compound aliases store field defaults and alias references.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, NamedCompoundAlias_015)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "endpoint_cfg = {\n"
        "    host: string(length=32) = \"localhost\",\n"
        "    port: uint16 = 8080\n"
        "}\n"
        "option endpoint {\n"
        "    type = endpoint_cfg\n"
        "    help = \"Endpoint configuration\"\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->nnamed_types, 1u);
    EXPECT_STREQ(s->named_types[0].name, "endpoint_cfg");
    EXPECT_EQ(s->named_types[0].expr.base, CF_TYPE_COMPOUND);
    ASSERT_EQ(s->named_types[0].expr.nfields, 2u);
    EXPECT_STREQ(s->named_types[0].expr.fields[0].name, "host");
    EXPECT_EQ(s->named_types[0].expr.fields[0].str_len, 32u);
    EXPECT_TRUE(s->named_types[0].expr.fields[0].has_default);
    EXPECT_STREQ(s->named_types[0].expr.fields[0].default_val, "localhost");
    EXPECT_STREQ(s->named_types[0].expr.fields[1].default_val, "8080");

    ASSERT_EQ(s->noptions, 1u);
    EXPECT_EQ(s->options[0].type.base, CF_TYPE_ALIAS);
    EXPECT_STREQ(s->options[0].type.alias_name, "endpoint_cfg");
}

/* ---------------------------------------------------------------------------
 * 016 — Subcommands, groups, positionals, and conditional content are parsed.
 * ------------------------------------------------------------------------- */
TEST(CfParseOption, ConditionalAndSubcommandStructures_016)
{
    auto s = std::make_unique<cf_schema_file_t>();
    std::memset(s.get(), 0, sizeof(*s));

    ASSERT_EQ(parse_src(
        "ifkey FEATURE == enabled || ifndef LEGACY {\n"
        "    section \"Global\" {\n"
        "        description = \"Global settings\"\n"
        "        option config {\n"
        "            type = file\n"
        "            help = \"Config file\"\n"
        "        }\n"
        "    }\n"
        "}\n"
        "subcommand deploy {\n"
        "    brief       = \"Deploy app\"\n"
        "    description = \"Detailed deploy\"\n"
        "    deprecated  = \"Use release\"\n"
        "    option force {\n"
        "        type = flag\n"
        "        help = \"Force deploy\"\n"
        "    }\n"
        "    section \"Runtime\" {\n"
        "        option env {\n"
        "            type = string\n"
        "            help = \"Environment\"\n"
        "        }\n"
        "    }\n"
        "    positional target {\n"
        "        type     = path\n"
        "        required = mandatory\n"
        "        multiple = 1..3\n"
        "        help     = \"Targets\"\n"
        "        details  = \"Deployment targets\"\n"
        "    }\n"
        "    group mode {\n"
        "        mandatory\n"
        "        options = [force, env]\n"
        "    }\n"
        "}\n", s.get()), 0);

    ASSERT_EQ(s->nsections, 1u);
    EXPECT_STREQ(s->sections[0].name, "Global");
    EXPECT_STREQ(s->sections[0].description, "Global settings");
    ASSERT_EQ(s->sections[0].noptions, 1u);
    EXPECT_STREQ(s->sections[0].options[0].name, "config");

    ASSERT_EQ(s->nsubcommands, 1u);
    EXPECT_STREQ(s->subcommands[0].name, "deploy");
    EXPECT_STREQ(s->subcommands[0].brief, "Deploy app");
    EXPECT_STREQ(s->subcommands[0].description, "Detailed deploy");
    EXPECT_TRUE(s->subcommands[0].is_deprecated);
    EXPECT_STREQ(s->subcommands[0].deprecated, "Use release");
    ASSERT_EQ(s->subcommands[0].noptions, 1u);
    EXPECT_STREQ(s->subcommands[0].options[0].name, "force");
    ASSERT_EQ(s->subcommands[0].nsections, 1u);
    EXPECT_STREQ(s->subcommands[0].sections[0].name, "Runtime");
    ASSERT_EQ(s->subcommands[0].sections[0].noptions, 1u);
    EXPECT_STREQ(s->subcommands[0].sections[0].options[0].name, "env");
    ASSERT_EQ(s->subcommands[0].npositionals, 1u);
    EXPECT_STREQ(s->subcommands[0].positionals[0].name, "target");
    EXPECT_EQ(s->subcommands[0].positionals[0].required, CF_REQ_MANDATORY);
    EXPECT_TRUE(s->subcommands[0].positionals[0].multiple.enabled);
    EXPECT_EQ(s->subcommands[0].positionals[0].multiple.min, 1u);
    EXPECT_EQ(s->subcommands[0].positionals[0].multiple.max, 3u);
    EXPECT_STREQ(s->subcommands[0].positionals[0].details, "Deployment targets");
    ASSERT_EQ(s->subcommands[0].ngroups, 1u);
    EXPECT_TRUE(s->subcommands[0].groups[0].mandatory);
    EXPECT_EQ(s->subcommands[0].groups[0].nmembers, 2u);
    EXPECT_STREQ(s->subcommands[0].groups[0].members[0], "force");
    EXPECT_STREQ(s->subcommands[0].groups[0].members[1], "env");
}
