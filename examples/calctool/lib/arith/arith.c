/* ============================================================================
 * lib/arith/arith.c  —  Basic arithmetic operators for calctool.
 *
 * KEY TEACHING POINT — how cliforge lib-options work:
 *
 *   1. cliforge generates cmdline_arith.h / cmdline_arith.c from arith.cf.
 *      Those files know nothing about calctool; they parse a self-contained
 *      set of options (--div-zero, --epsilon, --rounding).
 *
 *   2. calctool's main() calls cf_argv_slice("arith", original_argc, argv, ...)
 *      which scans the command line for every --arith.<X> token and yields
 *      a new argc/argv where the "arith." prefix is stripped:
 *        --arith.div-zero=inf  →  --div-zero=inf
 *
 *   3. That sub-argv is passed here to arith_init(), which calls the
 *      generated arith_cmdline_parse().  From this point on, all arith
 *      behaviour is driven by g_opts — a private struct fully opaque to
 *      the rest of the program.
 *
 *   4. arith_div() reads g_opts.div_zero to decide what to return for x/0.
 *      arith_clamp_epsilon() reads g_opts.epsilon.
 *      arith_round() reads g_opts.rounding.
 *      No other translation unit touches these fields.
 * ========================================================================= */

#include "arith.h"
#include "cmdline_arith.h"   /* cliforge-generated */

#include <stdio.h>
#include <math.h>
#include <string.h>

/* ── Private state ─────────────────────────────────────────────────────── */

static struct arith_cmdline g_opts;   /* populated by arith_init()        */
static int                  g_ready;  /* 1 after successful arith_init()   */

/* Rounding rule indices (inline enum in arith.cf, stored as int 0-based) */
#define ARITH_ROUND_NEAREST     0
#define ARITH_ROUND_UP          1
#define ARITH_ROUND_DOWN        2
#define ARITH_ROUND_TOWARD_ZERO 3
#define ARITH_ROUND_TOWARD_INF  4

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

int arith_init(int argc, char **argv)
{
    int rc;

    g_ready = 0;
    memset(&g_opts, 0, sizeof(g_opts));

    /*
     * arith_cmdline_parse() is the cliforge-generated parser.
     * It fills g_opts from the (already-sliced) argc/argv.
     * It returns:
     *    0  — parsed OK
     *    1  — --help or --version printed (treat as success for lib use)
     *   -1  — parse error
     */
    rc = arith_cmdline_parse(argc, (char *const *)argv, &g_opts);
    if (rc < 0) {
        fprintf(stderr, "arith: option parse failed\n");
        return -1;
    }

    g_ready = 1;

    fprintf(stdout,
            "[arith] init OK: div-zero=%d  epsilon=%g  rounding=%d\n",
            (int)g_opts.div_zero, g_opts.epsilon, g_opts.rounding);
    return 0;
}

void arith_shutdown(void)
{
    g_ready = 0;
}

/* ── Helpers ───────────────────────────────────────────────────────────── */

double arith_clamp_epsilon(double x)
{
    double eps = g_ready ? g_opts.epsilon : 1.0e-9;
    if (x < 0.0) x = -x < eps ? 0.0 : x;
    else          x =  x < eps ? 0.0 : x;
    return x;
}

double arith_round(double x)
{
    int rule = g_ready ? g_opts.rounding : ARITH_ROUND_NEAREST;
    switch (rule) {
    case ARITH_ROUND_UP:          return ceil(x);
    case ARITH_ROUND_DOWN:        return floor(x);
    case ARITH_ROUND_TOWARD_ZERO: return (x >= 0.0) ? floor(x) : ceil(x);
    case ARITH_ROUND_TOWARD_INF:  return (x >= 0.0) ? ceil(x)  : floor(x);
    case ARITH_ROUND_NEAREST:
    default:                      return floor(x + 0.5);
    }
}

/* ── Arithmetic operations ─────────────────────────────────────────────── */

double arith_add(double a, double b)
{
    return arith_clamp_epsilon(a + b);
}

double arith_sub(double a, double b)
{
    return arith_clamp_epsilon(a - b);
}

double arith_mul(double a, double b)
{
    return arith_clamp_epsilon(a * b);
}

double arith_div(double a, double b)
{
    static const double POS_INF =  1.0 / 0.0;  /* C99 */
    static const double NEG_INF = -1.0 / 0.0;
    static const double NAN_VAL =  0.0 / 0.0;
    static const double SAT_POS =  1.0e308;
    static const double SAT_NEG = -1.0e308;

    arith_div_zero_policy_t policy;

    if (b != 0.0)
        return arith_clamp_epsilon(a / b);

    policy = g_ready ? g_opts.div_zero : ARITH_div_zero_policy_ERROR;

    switch (policy) {
    case ARITH_div_zero_policy_NAN:
        return NAN_VAL;

    case ARITH_div_zero_policy_INF:
        return (a >= 0.0) ? POS_INF : NEG_INF;

    case ARITH_div_zero_policy_SATURATE:
        return (a >= 0.0) ? SAT_POS : SAT_NEG;

    case ARITH_div_zero_policy_ERROR:
    default:
        fprintf(stderr, "arith: division by zero (a=%g)\n", a);
        return 0.0;
    }
}
