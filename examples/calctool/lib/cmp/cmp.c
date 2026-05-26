/* ============================================================================
 * lib/cmp/cmp.c  —  Floating-point comparison operators for calctool.
 *
 * Same argv-slice pattern as arith.c — see arith.c for the full narrative.
 *
 * Options parsed here (via generated cmp_cmdline_parse):
 *   --epsilon    double   Comparison tolerance.          default: 1e-12
 *   --nan-equal  bool     Treat NaN == NaN as true.      default: false
 * ========================================================================= */

#include "cmp.h"
#include "cmdline_cmp.h"   /* cliforge-generated */

#include <stdio.h>
#include <math.h>
#include <string.h>

/* ── Private state ─────────────────────────────────────────────────────── */

static struct cmp_cmdline g_opts;
static int                g_ready;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

int cmp_init(int argc, char **argv)
{
    int rc;
    g_ready = 0;
    memset(&g_opts, 0, sizeof(g_opts));

    rc = cmp_cmdline_parse(argc, (char *const *)argv, &g_opts);
    if (rc < 0) {
        fprintf(stderr, "cmp: option parse failed\n");
        return -1;
    }

    g_ready = 1;

    fprintf(stdout,
            "[cmp]   init OK: epsilon=%g  nan-equal=%s\n",
            g_opts.epsilon, g_opts.nan_equal ? "true" : "false");
    return 0;
}

void cmp_shutdown(void)
{
    g_ready = 0;
}

/* ── Helpers ───────────────────────────────────────────────────────────── */

static double eps(void)
{
    return g_ready ? g_opts.epsilon : 1.0e-12;
}

/* Portable NaN check (no <math.h> isnan() in C89) */
static int is_nan(double x)
{
    return x != x;
}

/* ── Comparison operations ─────────────────────────────────────────────── */

int cmp_eq(double a, double b)
{
    /* NaN handling: IEEE 754 says NaN != NaN, but nan-equal=true overrides */
    if (is_nan(a) && is_nan(b))
        return g_ready ? g_opts.nan_equal : 0;
    if (is_nan(a) || is_nan(b))
        return 0;
    /* Epsilon comparison: |a - b| <= epsilon */
    {
        double diff = a - b;
        if (diff < 0.0) diff = -diff;
        return diff <= eps();
    }
}

int cmp_ne(double a, double b)
{
    return !cmp_eq(a, b);
}

int cmp_lt(double a, double b)
{
    /* a < b  iff  b - a > epsilon  (a is meaningfully less than b) */
    return (b - a) > eps();
}

int cmp_le(double a, double b)
{
    return !cmp_gt(a, b);
}

int cmp_gt(double a, double b)
{
    return (a - b) > eps();
}

int cmp_ge(double a, double b)
{
    return !cmp_lt(a, b);
}
