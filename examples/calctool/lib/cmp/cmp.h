/* ============================================================================
 * lib/cmp/cmp.h  —  Floating-point comparison operators for calctool.
 *
 * Statically linked.  Options sliced from argv under the "cmp" prefix:
 *   --cmp.epsilon=1e-9   --cmp.nan-equal=true
 * ========================================================================= */

#ifndef CMP_H
#define CMP_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * cmp_init - Parse cmp options and initialise the library.
 * @argc / @argv: filtered argv from cf_argv_slice("cmp", ...).
 * Returns 0 on success, -1 on error.
 */
int cmp_init(int argc, char **argv);
void cmp_shutdown(void);

/* -------------------------------------------------------------------------
 * Comparison functions — all respect configured epsilon and nan-equal.
 * Return 1 (true) or 0 (false).
 * ---------------------------------------------------------------------- */

/** cmp_eq - True if |a - b| <= epsilon (or both NaN when nan-equal=true). */
int cmp_eq(double a, double b);

/** cmp_ne - True if !cmp_eq(a, b). */
int cmp_ne(double a, double b);

/** cmp_lt - True if a < b - epsilon. */
int cmp_lt(double a, double b);

/** cmp_le - True if a <= b + epsilon. */
int cmp_le(double a, double b);

/** cmp_gt - True if a > b + epsilon. */
int cmp_gt(double a, double b);

/** cmp_ge - True if a >= b - epsilon. */
int cmp_ge(double a, double b);

#ifdef __cplusplus
}
#endif

#endif /* CMP_H */
