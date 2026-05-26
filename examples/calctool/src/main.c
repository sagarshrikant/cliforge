/* ============================================================================
 * examples/calctool/src/main.c
 *
 * Full calctool example — demonstrates the complete cliforge multi-lib
 * option-passing pattern:
 *
 *   STATIC libs  (linked at compile time):
 *     arith  — arithmetic ops:  +  -  *  /  with configurable div-zero policy
 *     cmp    — comparison ops:  < > == != with epsilon tolerance
 *
 *   SHARED libs  (linked at compile time, loaded by dynamic linker at start):
 *     trig   — sin/cos/tan with configurable angle unit (rad/deg/grad/turns)
 *     stats  — mean/variance/stddev/median with sample vs population formula
 *
 *   DLOPEN plugin (discovered and loaded at runtime):
 *     ops_linear — dot product, L2 norm, matrix-vector multiply
 *                  loaded from libops_linear.so via --plugin
 *
 * HOW OPTIONS FLOW:
 *
 *   calctool parses its own argv with cc_cmdline_parse() (the main schema).
 *   For each library, cf_argv_slice() extracts that lib's namespace tokens
 *   and strips the prefix:
 *
 *     --arith.div-zero=inf  =>  arith argv: --div-zero=inf
 *     --trig.units=degrees  =>  trig  argv: --units=degrees
 *
 *   Each lib's init() receives that filtered argv and calls its own generated
 *   cmdline_parse().  After init the rest of the program calls the math API.
 *
 * RUN EXAMPLES:
 *   ./calctool eval
 *   ./calctool --arith.div-zero=nan eval
 *   ./calctool --trig.units=degrees eval
 *   ./calctool --stats.mode=population eval
 *   ./calctool --plugin name=ops_linear,path=./libops_linear.so eval
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "cmdline.h"         /* cliforge-generated main schema */
#include "arith.h"           /* static lib */
#include "cmp.h"             /* static lib */
#include "trig.h"            /* shared lib */
#include "stats.h"           /* shared lib */
#include "ops_linear.h"      /* dlopen plugin contract */
#include <cliforge/cf_argv_slice.h>  /* cliforge SDK: argv-slice helper for @import */

#ifdef __linux__
#  include <dlfcn.h>
#  define HAVE_DLOPEN 1
#else
#  define HAVE_DLOPEN 0
#endif

/* ── Plugin state ──────────────────────────────────────────────────────── */

typedef int (*ops_init_fn)(int, char **, ops_linear_api_t *);

static void           *g_plugin_handle;
static ops_linear_api_t g_ops;
static int              g_ops_loaded;

static void load_plugin(const char *path, int pl_argc, char **pl_argv)
{
#if HAVE_DLOPEN
    union { void *obj; ops_init_fn fn; } sym;

    printf("\n[plugin] loading %s\n", path);
    g_plugin_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!g_plugin_handle) {
        fprintf(stderr, "[plugin] dlopen: %s\n", dlerror());
        return;
    }
    sym.obj = dlsym(g_plugin_handle, "ops_linear_init");
    if (!sym.obj) {
        fprintf(stderr, "[plugin] dlsym: %s\n", dlerror());
        dlclose(g_plugin_handle);
        g_plugin_handle = NULL;
        return;
    }
    memset(&g_ops, 0, sizeof(g_ops));
    if (sym.fn(pl_argc, pl_argv, &g_ops) != 0) {
        fprintf(stderr, "[plugin] init failed\n");
        dlclose(g_plugin_handle);
        g_plugin_handle = NULL;
        return;
    }
    g_ops_loaded = 1;
    printf("[plugin] loaded OK (version %s)\n", g_ops.version);
#else
    (void)path; (void)pl_argc; (void)pl_argv;
    fprintf(stderr, "[plugin] dlopen not available on this platform\n");
#endif
}

static void unload_plugin(void)
{
#if HAVE_DLOPEN
    if (g_plugin_handle) { dlclose(g_plugin_handle); g_plugin_handle = NULL; }
#endif
    g_ops_loaded = 0;
}

/* ── Demo computations ─────────────────────────────────────────────────── */

static void demo_arith(void)
{
    printf("\n  [arith]\n");
    printf("    3  + 4     = %g\n",  arith_add(3.0,  4.0));
    printf("    10 - 3.5   = %g\n",  arith_sub(10.0, 3.5));
    printf("    6  * 7     = %g\n",  arith_mul(6.0,  7.0));
    printf("    22 / 7     = %g\n",  arith_div(22.0, 7.0));
    printf("    1  / 0     = %g    (div-zero policy)\n", arith_div(1.0, 0.0));
    printf("    round(2.5) = %g    (rounding rule)\n",   arith_round(2.5));
}

static void demo_cmp(void)
{
    double nan_val = 0.0 / 0.0;
    printf("\n  [cmp]\n");
    printf("    1.0 == 1.0             : %s\n", cmp_eq(1.0, 1.0)              ? "true" : "false");
    printf("    1.0 == 1.0 + 1e-15     : %s  (within epsilon)\n",
           cmp_eq(1.0, 1.0 + 1e-15) ? "true" : "false");
    printf("    1.0 == 1.0 + 0.1       : %s  (outside epsilon)\n",
           cmp_eq(1.0, 1.0 + 0.1)   ? "true" : "false");
    printf("    3.0 < 4.0              : %s\n", cmp_lt(3.0, 4.0)              ? "true" : "false");
    printf("    NaN == NaN             : %s  (nan-equal setting)\n",
           cmp_eq(nan_val, nan_val)  ? "true" : "false");
}

static void demo_trig(void)
{
    printf("\n  [trig]\n");
    printf("    sin(0)      = %g\n",  trig_sin(0.0));
    printf("    cos(0)      = %g\n",  trig_cos(0.0));
    printf("    sin(90)     = %g    (angle in configured units)\n", trig_sin(90.0));
    printf("    cos(180)    = %g\n",  trig_cos(180.0));
    printf("    atan2(1,1)  = %g    (result in configured units)\n",
           trig_atan2(1.0, 1.0));
}

static void demo_stats(void)
{
    double data[6]  = { 4.0, 8.0, 15.0, 16.0, 23.0, 42.0 };
    double tmp[6];
    printf("\n  [stats]  dataset: 4  8  15  16  23  42\n");
    printf("    mean     = %g\n", stats_mean(data, 6));
    printf("    variance = %g    (sample/population setting)\n",
           stats_variance(data, 6));
    printf("    stddev   = %g\n", stats_stddev(data, 6));
    memcpy(tmp, data, sizeof(data));
    printf("    median   = %g\n", stats_median(tmp, 6));
}

static void demo_plugin(void)
{
    double v1[4]  = { 1.0, 2.0, 3.0, 4.0 };
    double v2[4]  = { 4.0, 3.0, 2.0, 1.0 };
    double M[4]   = { 1.0, 2.0, 3.0, 4.0 };  /* 2x2 row-major */
    double out[2] = { 0.0, 0.0 };
    printf("\n  [ops_linear]  (dlopen plugin)\n");
    printf("    dot([1,2,3,4],[4,3,2,1]) = %g\n", g_ops.dot(v1, v2, 4));
    printf("    norm([1,2,3,4])          = %g\n", g_ops.norm(v1, 4));
    g_ops.matvec(M, 2, 2, v1, out);
    printf("    [[1,2],[3,4]] * [1,2]    = [%g, %g]\n", out[0], out[1]);
}

/* ── Subcommand handlers ────────────────────────────────────────────────── */

static void do_eval(const struct cc_cmdline *args)
{
    printf("  echo        : %s\n", args->eval.echo ? "yes" : "no");
    printf("  max-depth   : %u\n", (unsigned int)args->eval.max_depth);

    demo_arith();
    demo_cmp();
    demo_trig();
    demo_stats();

    if (g_ops_loaded)
        demo_plugin();
    else
        printf("\n  [ops_linear] not loaded — pass --plugin name=ops_linear,path=./libops_linear.so\n");
}

static void do_bench(const struct cc_cmdline *args)
{
    unsigned int i;
    double acc = 0.0;
    printf("  plugin-name     : %s\n",  args->bench.plugin_name);
    printf("  iterations      : %u\n",  (unsigned int)args->bench.iterations);
    printf("  warmup          : %u\n",  (unsigned int)args->bench.warmup);
    printf("  report-interval : %llu\n",(unsigned long long)args->bench.report_interval);
    printf("\n  Benchmarking arith_add for %u iterations...\n",
           (unsigned int)args->bench.iterations);
    for (i = 0; i < args->bench.iterations; i++)
        acc = arith_add(acc, 1.0);
    printf("  Sum = %g\n", acc);
}

static void do_evaluate(const struct cc_cmdline *args)
{
    int i;
    printf("  expressions: %d\n", args->evaluate.expressions_count);
    for (i = 0; i < args->evaluate.expressions_count; i++)
        printf("    [%d] %s\n", i, args->evaluate.expressions[i]);
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    struct cc_cmdline args;
    int rc, i;

    /* ── 1. Parse calctool's own options ─────────────────────────────── */
    memset(&args, 0, sizeof(args));
    rc = cc_cmdline_parse(argc, argv, &args);
    if (rc != 0)
        return (rc > 0) ? 0 : 1;

    /* ── 2. Slice argv and init each static lib ──────────────────────── */
    printf("=== Library init ===\n");

    /* ARITH (static):
     * cf_argv_slice scans argv for --arith.* tokens, strips "arith."
     * and hands --div-zero=... --epsilon=... to arith_init().
     * arith_init() calls arith_cmdline_parse() internally. */
    {
        char *av[CF_SLICE_MAX_ARGS]; char sb[CF_SLICE_STRBUF]; int ac;
        ac = cf_argv_slice("arith", argc, argv, av, CF_SLICE_MAX_ARGS, sb, CF_SLICE_STRBUF);
        if (arith_init(ac, av) != 0) return 1;
    }

    /* CMP (static): same pattern */
    {
        char *av[CF_SLICE_MAX_ARGS]; char sb[CF_SLICE_STRBUF]; int ac;
        ac = cf_argv_slice("cmp", argc, argv, av, CF_SLICE_MAX_ARGS, sb, CF_SLICE_STRBUF);
        if (cmp_init(ac, av) != 0) return 1;
    }

    /* ── 3. Slice argv and init each shared lib ──────────────────────── */

    /* TRIG (shared -ltrig): identical pattern to static libs */
    {
        char *av[CF_SLICE_MAX_ARGS]; char sb[CF_SLICE_STRBUF]; int ac;
        ac = cf_argv_slice("trig", argc, argv, av, CF_SLICE_MAX_ARGS, sb, CF_SLICE_STRBUF);
        if (trig_init(ac, av) != 0) return 1;
    }

    /* STATS (shared -lstats) */
    {
        char *av[CF_SLICE_MAX_ARGS]; char sb[CF_SLICE_STRBUF]; int ac;
        ac = cf_argv_slice("stats", argc, argv, av, CF_SLICE_MAX_ARGS, sb, CF_SLICE_STRBUF);
        if (stats_init(ac, av) != 0) return 1;
    }

    /* ── 4. dlopen plugin(s) ─────────────────────────────────────────── */
    /* The main schema stores --plugin as a repeatable compound.
     * For each plugin whose name matches "ops_linear", slice the ops_linear.*
     * namespace from argv and pass it to the plugin's init via dlopen/dlsym. */
    for (i = 0; i < args.plugin_count; i++) {
        if (strcmp(args.plugin[i].name, "ops_linear") == 0) {
            char *av[CF_SLICE_MAX_ARGS]; char sb[CF_SLICE_STRBUF]; int ac;
            ac = cf_argv_slice("ops_linear", argc, argv,
                               av, CF_SLICE_MAX_ARGS, sb, CF_SLICE_STRBUF);
            load_plugin(args.plugin[i].path, ac, av);
        }
    }

    /* ── 5. Summary ──────────────────────────────────────────────────── */
    printf("\n=== Global options ===\n");
    printf("  verbosity : %d\n", (int)args.verbosity);
    printf("  precision : %u\n", (unsigned int)args.precision);
    printf("  seed      : %d\n", (int)args.seed);
    printf("  plugins   : %d\n", args.plugin_count);

    if (args.verbosity >= CC_verbosity_VERBOSE) {
        printf("\n=== Option dump ===\n");
        cc_cmdline_dump(&args);
    }

    /* ── 6. Subcommand ───────────────────────────────────────────────── */
    printf("\n=== %s ===\n",
           args.subcmd == CC_CMD_EVAL     ? "eval"     :
           args.subcmd == CC_CMD_BENCH    ? "bench"    :
           args.subcmd == CC_CMD_EVALUATE ? "evaluate" : "(no subcommand)");

    switch (args.subcmd) {
    case CC_CMD_EVAL:     do_eval(&args);     break;
    case CC_CMD_BENCH:    do_bench(&args);    break;
    case CC_CMD_EVALUATE: do_evaluate(&args); break;
    case CC_CMD_NONE:
    default:
        printf("No subcommand — try: %s eval --help\n", argv[0]);
        break;
    }

    /* ── 7. Cleanup ──────────────────────────────────────────────────── */
    unload_plugin();
    arith_shutdown();
    cmp_shutdown();
    trig_shutdown();
    stats_shutdown();

    return 0;
}
