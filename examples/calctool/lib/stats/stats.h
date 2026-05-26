/* ============================================================================
 * lib/stats/stats.h  —  Basic statistics functions for calctool.
 *
 * Shared library (-lstats).  Options under the "stats" prefix:
 *   --stats.mode=population   --stats.max-points=500000
 * ========================================================================= */

#ifndef STATS_H
#define STATS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * stats_init - Parse stats options and initialise the library.
 * @argc / @argv: filtered argv from cf_argv_slice("stats", ...).
 * Returns 0 on success, -1 on error.
 */
int stats_init(int argc, char **argv);
void stats_shutdown(void);

/* -------------------------------------------------------------------------
 * Statistical functions.
 * All accept a pointer to an array of @n doubles.
 * Return NaN when @n == 0 or @n exceeds configured max-points.
 * ---------------------------------------------------------------------- */

/** stats_mean   - Arithmetic mean. */
double stats_mean(const double *data, unsigned int n);

/**
 * stats_variance - Variance.
 * Uses (n-1) denominator when mode=sample (Bessel-corrected),
 * uses (n)   denominator when mode=population.
 */
double stats_variance(const double *data, unsigned int n);

/** stats_stddev - Standard deviation (sqrt of variance). */
double stats_stddev(const double *data, unsigned int n);

/**
 * stats_median - Median.
 * WARNING: @data is sorted in-place for efficiency (no allocation).
 */
double stats_median(double *data, unsigned int n);

#ifdef __cplusplus
}
#endif

#endif /* STATS_H */
