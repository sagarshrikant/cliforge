/* ============================================================================
 * lib/stats/stats.c  —  Basic statistics functions for calctool.
 *
 * SHARED LIBRARY — built as -lstats, linked at build time.
 *
 * Options parsed by generated stats_cmdline_parse():
 *   --mode        enum    sample|population   default: sample
 *   --max-points  uint32  max data-set size   default: 1_000_000
 *   --epsilon     double  convergence tol.    default: 1e-10
 * ========================================================================= */

#include "stats.h"
#include "cmdline_stats.h"   /* cliforge-generated */

#include <stdio.h>
#include <math.h>
#include <string.h>

/* ── Private state ─────────────────────────────────────────────────────── */

static struct stats_cmdline g_opts;
static int                  g_ready;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

int stats_init(int argc, char **argv)
{
    int rc;
    const char *mode_names[] = { "sample", "population" };

    g_ready = 0;
    memset(&g_opts, 0, sizeof(g_opts));

    rc = stats_cmdline_parse(argc, (char *const *)argv, &g_opts);
    if (rc < 0) {
        fprintf(stderr, "stats: option parse failed\n");
        return -1;
    }

    g_ready = 1;

    fprintf(stdout,
            "[stats] init OK: mode=%s  max-points=%u  epsilon=%g\n",
            mode_names[g_opts.mode],
            (unsigned int)g_opts.max_points,
            g_opts.epsilon);
    return 0;
}

void stats_shutdown(void)
{
    g_ready = 0;
}

/* ── Validation helper ─────────────────────────────────────────────────── */

static int check_n(unsigned int n)
{
    unsigned int limit = g_ready ? (unsigned int)g_opts.max_points : 1000000U;
    if (n == 0) {
        fprintf(stderr, "stats: empty data set\n");
        return 0;
    }
    if (n > limit) {
        fprintf(stderr, "stats: data set size %u exceeds max-points %u\n",
                n, limit);
        return 0;
    }
    return 1;
}

static double nan_val(void)
{
    /* portable NaN */
    static const double z = 0.0;
    return z / z;
}

/* ── Statistical functions ─────────────────────────────────────────────── */

double stats_mean(const double *data, unsigned int n)
{
    double sum = 0.0;
    unsigned int i;
    if (!check_n(n)) return nan_val();
    for (i = 0; i < n; i++) sum += data[i];
    return sum / (double)n;
}

double stats_variance(const double *data, unsigned int n)
{
    double mean, sum_sq, diff;
    unsigned int i;
    unsigned int denom;

    if (!check_n(n)) return nan_val();

    /* sample mode requires at least 2 points */
    if (g_ready && g_opts.mode == STATS_sampling_mode_SAMPLE && n < 2) {
        fprintf(stderr, "stats: variance(sample) requires n >= 2\n");
        return nan_val();
    }

    mean = stats_mean(data, n);
    sum_sq = 0.0;
    for (i = 0; i < n; i++) {
        diff = data[i] - mean;
        sum_sq += diff * diff;
    }

    /* Bessel's correction: divide by (n-1) for sample, n for population */
    denom = (g_ready && g_opts.mode == STATS_sampling_mode_POPULATION)
            ? n : (n - 1);
    return sum_sq / (double)denom;
}

double stats_stddev(const double *data, unsigned int n)
{
    double var = stats_variance(data, n);
    if (var != var) return var;   /* propagate NaN */
    return sqrt(var);
}

/* Simple insertion sort — good enough for demo data sets */
static void isort(double *a, unsigned int n)
{
    unsigned int i, j;
    for (i = 1; i < n; i++) {
        double key = a[i];
        j = i;
        while (j > 0 && a[j - 1] > key) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = key;
    }
}

double stats_median(double *data, unsigned int n)
{
    if (!check_n(n)) return nan_val();
    isort(data, n);
    if (n % 2 == 1)
        return data[n / 2];
    return (data[n / 2 - 1] + data[n / 2]) / 2.0;
}
