/* ============================================================================
 * utest_cf_generate.cpp — unit tests for cf_generate()
 *
 * Strategy: full pipeline (lex → parse → generate) into a temp dir, then
 * read the .h / .c / .md files and assert on key substrings.
 *
 * Observed generator output patterns (verified against live generator):
 *   struct:        "struct CT_cmdline {"   (no typedef)
 *   parse fn:      "CT_cmdline_parse"
 *   help fn:       "CT_cmdline_help"
 *   enum members:  stored as plain int field in struct, enum values defined
 *                  as macros or enum in a separate block
 *   include guard: "#ifndef CT_CMDLINE_H"
 * ========================================================================= */
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

extern "C" {
#include "cf_lex.h"
#include "cf_parse.h"
#include "cf_gen.h"
#include "cf_ast.h"
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int run_pipeline(const char *src, const char *out_dir)
{
    cf_lexer_t lex;
    cf_lex_init(&lex, src,
                static_cast<unsigned int>(std::strlen(src)),
                "<test>");
    if (cf_lex_run(&lex) != 0)
        return -10;

    /* cf_schema_file_t is ~37 MB — always heap-allocate. */
    auto schema = std::make_unique<cf_schema_file_t>();
    std::memset(schema.get(), 0, sizeof(*schema));

    int nerr = cf_parse(lex.tokens, lex.ntokens, "<test>", schema.get());
    if (nerr > 0)
        return -20;

    cf_gen_options_t opts{};
    opts.output_dir = out_dir;
    opts.dry_run    = 0;
    opts.verbose    = 0;

    return cf_generate(schema.get(), &opts);
}

static std::string read_file(const std::string &path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

static std::string make_tmpdir()
{
    char tmpl[] = "/tmp/cf_gen_test_XXXXXX";
    char *p = mkdtemp(tmpl);
    return p ? std::string(p) : std::string{};
}

static void rm_tmpdir(const std::string &dir)
{
    if (dir.empty() || dir.rfind("/tmp/", 0) != 0) return;
    (void)std::system(("rm -rf " + dir).c_str()); /* LCOV_EXCL_LINE */
}

/* Minimal schema — prefix "CT", output "cmdline". */
static std::string make_schema(const char *extra = "")
{
    return std::string(
        "@schema 1\n"
        "meta {\n"
        "    app    = \"calctool\"\n"
        "    brief  = \"Expression evaluator\"\n"
        "    prefix = \"CT\"\n"
        "    output = \"cmdline\"\n"
        "}\n") + extra;
}

/* ---------------------------------------------------------------------------
 * 001 — Include guard contains PREFIX and output name.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, HeaderGuard_001)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema().c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    EXPECT_FALSE(h.empty());
    EXPECT_NE(h.find("#ifndef"), std::string::npos);
    EXPECT_NE(h.find("#define"), std::string::npos);
    EXPECT_NE(h.find("#endif"),  std::string::npos);
    /* Guard name derived from prefix + output: CT_CMDLINE_H */
    EXPECT_NE(h.find("CT_CMDLINE_H"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 002 — Generated header declares the args struct (no typedef — bare struct).
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, ArgsStructPresent_002)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema(
        "option workers {\n"
        "    type = uint32\n"
        "    help = \"Workers\"\n"
        "}\n").c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    /* Generator emits: "struct CT_cmdline {" */
    EXPECT_NE(h.find("struct CT_cmdline"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 003 — Generated .c file includes the matching header.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, SourceIncludesHeader_003)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema().c_str(), dir.c_str()), 0);

    std::string c = read_file(dir + "/cmdline.c");
    EXPECT_NE(c.find("#include \"cmdline.h\""), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 004 — FLAG option: field name appears in the header struct.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, FlagOptionInStruct_004)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema(
        "option verbose {\n"
        "    type = flag\n"
        "    help = \"Verbose mode\"\n"
        "}\n").c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    EXPECT_NE(h.find("verbose"), std::string::npos);

    std::string c = read_file(dir + "/cmdline.c");
    EXPECT_NE(c.find("verbose"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 005 — UINT32 option: uint32_t field type appears in the header.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, Uint32FieldType_005)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema(
        "option workers {\n"
        "    type    = uint32\n"
        "    default = 4\n"
        "    help    = \"Thread count\"\n"
        "}\n").c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    EXPECT_NE(h.find("uint32_t"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 006 — Parse function name: <PREFIX>_cmdline_parse.
 *        Generator uses the literal prefix string (case preserved).
 *        prefix = "CT" → function = "CT_cmdline_parse".
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, ParseFunctionName_006)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema().c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    EXPECT_NE(h.find("CT_cmdline_parse"), std::string::npos);

    std::string c = read_file(dir + "/cmdline.c");
    EXPECT_NE(c.find("CT_cmdline_parse"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 007 — Help function name: <PREFIX>_cmdline_help.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, HelpFunctionName_007)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema().c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    EXPECT_NE(h.find("CT_cmdline_help"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 008 — Generated .md file begins with a Markdown heading '#'.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, MarkdownH1_008)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema().c_str(), dir.c_str()), 0);

    std::string md = read_file(dir + "/cmdline.md");
    EXPECT_FALSE(md.empty());
    EXPECT_EQ(md[0], '#');

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 009 — All three output files are created (.h, .c, .md).
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, AllThreeFilesCreated_009)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema().c_str(), dir.c_str()), 0);

    struct stat st{};
    EXPECT_EQ(stat((dir + "/cmdline.h").c_str(),  &st), 0) << "cmdline.h missing";
    EXPECT_EQ(stat((dir + "/cmdline.c").c_str(),  &st), 0) << "cmdline.c missing";
    EXPECT_EQ(stat((dir + "/cmdline.md").c_str(), &st), 0) << "cmdline.md missing";

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 010 — Default value literal appears somewhere in the .c file.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, DefaultValueInSource_010)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema(
        "option workers {\n"
        "    type    = uint32\n"
        "    default = 4\n"
        "    help    = \"Thread count\"\n"
        "}\n").c_str(), dir.c_str()), 0);

    std::string c = read_file(dir + "/cmdline.c");
    EXPECT_NE(c.find("4"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 011 — Invalid output directory: cf_generate() returns non-zero.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, BadOutputDir_011)
{
    int rc = run_pipeline(make_schema().c_str(),
                          "/nonexistent_cf_gen_test_xyz");
    EXPECT_NE(rc, 0);
}

/* ---------------------------------------------------------------------------
 * 012 — Inline choice option: field appears as plain int in the struct.
 *
 * For inline (anonymous) choices, the generator emits:
 *     int    level;
 * Member names (debug/info/warn/error) are NOT emitted in the header —
 * they exist only as accepted string values in the parser body.
 * A named enum type would be required for typed enum constants in the header.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, EnumOptionInHeader_012)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema(
        "option level {\n"
        "    type    = (debug, info, warn, error)\n"
        "    default = info\n"
        "    help    = \"Log level\"\n"
        "}\n").c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    /* Inline choice field is emitted as "int\tlevel;" in the struct. */
    EXPECT_NE(h.find("level"), std::string::npos);
    /* The field type is int (not a typedef'd enum). */
    EXPECT_NE(h.find("int"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 013 — app name appears in the .md document.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, AppNameInMarkdown_013)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema().c_str(), dir.c_str()), 0);

    std::string md = read_file(dir + "/cmdline.md");
    EXPECT_NE(md.find("calctool"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 014 — Named choice aliases emit enum types and enum-based defaults.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, NamedChoiceAlias_014)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema(
        "mode_choice = (fast, slow)\n"
        "option mode {\n"
        "    type    = mode_choice\n"
        "    default = slow\n"
        "    help    = \"Mode\"\n"
        "}\n").c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    std::string c = read_file(dir + "/cmdline.c");
    EXPECT_NE(h.find("typedef enum CT_mode_choice"), std::string::npos);
    EXPECT_NE(h.find("CT_mode_choice_SLOW"), std::string::npos);
    EXPECT_NE(h.find("CT_mode_choice_t"), std::string::npos);
    EXPECT_NE(c.find("out->mode = CT_mode_choice_SLOW;"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 015 — Compound aliases, multiple values, and sensitive dumps are generated.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, CompoundAliasMultipleAndSensitive_015)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    ASSERT_EQ(run_pipeline(make_schema(
        "endpoint_cfg = {\n"
        "    host: string(length=32),\n"
        "    port: uint16\n"
        "}\n"
        "option endpoint {\n"
        "    type = endpoint_cfg\n"
        "    help = \"Endpoint\"\n"
        "}\n"
        "option endpoints {\n"
        "    type     = endpoint_cfg\n"
        "    multiple = 2\n"
        "    help     = \"Endpoints\"\n"
        "}\n"
        "option token {\n"
        "    type      = string\n"
        "    sensitive = true\n"
        "    help      = \"Secret token\"\n"
        "}\n").c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    std::string c = read_file(dir + "/cmdline.c");
    EXPECT_NE(h.find("typedef struct CT_endpoint_cfg"), std::string::npos);
    EXPECT_NE(h.find("endpoint;"), std::string::npos);
    EXPECT_NE(h.find("struct CT_endpoint_cfg\tendpoints[2];"), std::string::npos);
    EXPECT_NE(h.find("endpoints_count"), std::string::npos);
    EXPECT_NE(c.find("cf__parse_compound"), std::string::npos);
    EXPECT_NE(c.find("token=***"), std::string::npos);

    rm_tmpdir(dir);
}

/* ---------------------------------------------------------------------------
 * 016 — Sections, subcommands, positionals, and markdown details are emitted.
 * ------------------------------------------------------------------------- */
TEST(CfGenerate, SectionsSubcommandsAndMarkdown_016)
{
    std::string dir = make_tmpdir();
    ASSERT_FALSE(dir.empty());

    const std::string schema =
        std::string(
        "@schema 1\n"
        "meta {\n"
        "    app         = \"calctool\"\n"
        "    brief       = \"Expression evaluator\"\n"
        "    prefix      = \"CT\"\n"
        "    output      = \"cmdline\"\n"
        "    doc-title   = \"CLI Reference\"\n"
        "    description = \"Long form reference\"\n"
        "    version     = \"1.2.3\"\n"
        "    author      = \"Alice\"\n"
        "}\n"
        "section \"General\" {\n"
        "    description = \"General settings\"\n"
        "    option config {\n"
        "        type    = file\n"
        "        short   = 'c'\n"
        "        default = \"cfg.yml\"\n"
        "        help    = \"Config file\"\n"
        "        details = \"Used at startup\"\n"
        "        example = \"--config cfg.yml\"\n"
        "        since   = \"1.0\"\n"
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
        "        option replicas {\n"
        "            type = uint32\n"
        "            help = \"Replica count\"\n"
        "        }\n"
        "    }\n"
        "    positional target {\n"
        "        type     = path\n"
        "        multiple = 1..2\n"
        "        help     = \"Targets\"\n"
        "    }\n"
        "}\n");

    ASSERT_EQ(run_pipeline(schema.c_str(), dir.c_str()), 0);

    std::string h = read_file(dir + "/cmdline.h");
    std::string c = read_file(dir + "/cmdline.c");
    std::string md = read_file(dir + "/cmdline.md");

    EXPECT_NE(h.find("CT_subcmd_t subcmd;"), std::string::npos);
    EXPECT_NE(h.find("target_count"), std::string::npos);
    EXPECT_NE(h.find("} deploy;"), std::string::npos);
    EXPECT_NE(c.find("Usage: calctool [OPTIONS] <subcommand>"), std::string::npos);
    EXPECT_NE(c.find("Subcommands:"), std::string::npos);
    EXPECT_NE(c.find("out->subcmd = CT_CMD_DEPLOY;"), std::string::npos);
    EXPECT_NE(md.find("# CLI Reference"), std::string::npos);
    EXPECT_NE(md.find("Long form reference"), std::string::npos);
    EXPECT_NE(md.find("### General"), std::string::npos);
    EXPECT_NE(md.find("Used at startup"), std::string::npos);
    EXPECT_NE(md.find("**Example:** `--config cfg.yml`"), std::string::npos);
    EXPECT_NE(md.find("*Since 1.0*"), std::string::npos);
    EXPECT_NE(md.find("## Subcommands"), std::string::npos);
    EXPECT_NE(md.find("> **Deprecated:** Use release"), std::string::npos);

    rm_tmpdir(dir);
}
