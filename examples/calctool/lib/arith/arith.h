/* ============================================================================
 * lib/arith/arith.h  —  Basic arithmetic operators for calctool.
 *
 * Statically linked into the calctool binary.
 * Options are supplied at startup via the argv slice for the "arith" prefix:
 *   calctool --arith.div-zero=inf --arith.epsilon=1e-6 ...
 *
 * The main application calls cf_argv_slice("arith", ...) to extract those
 * options and passes the result to arith_init().  arith_init() then calls
 * the cliforge-generated arith_cmdline_parse() to populate its private
 * options struct.  No other part of the program needs to know arith's
 * option names or types.
 * ========================================================================= */

#ifndef ARITH_H
#define ARITH_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * arith_init - Parse arith options and initialise the library.
 *
 * @argc / @argv: filtered argv produced by cf_argv_slice("arith", ...).
 *   Options expected: --div-zero=<nan|inf|error|saturate>
 *                     --epsilon=<double>
 *                     --rounding=<nearest|up|down|toward-zero|toward-inf>
 *
 * Returns 0 on success, -1 on parse error (message printed to stderr).
 */
int arith_init(int argc, char **argv);

/** arith_shutdown - Release any resources held by the library. */
void arith_shutdown(void);

/* -------------------------------------------------------------------------
 * Arithmetic operations
 * All functions apply the configured epsilon-clamp to results.
 * ---------------------------------------------------------------------- */

double arith_add(double a, double b);
double arith_sub(double a, double b);
double arith_mul(double a, double b);

/**
 * arith_div - Divide @a by @b, applying the configured div-zero policy:
 *   error    — prints a message to stderr and returns 0.0
 *   nan      — returns IEEE 754 NaN.
 *   inf      — returns +Inf or -Inf (sign of @a).
 *   saturate — returns ±1e308 (largest finite double).
 */
double arith_div(double a, double b);

/**
 * arith_round - Round @x using the configured rounding rule:
 *   nearest    — round to nearest, ties to even (default)
 *   up         — ceiling
 *   down       — floor
 *   toward-zero — truncate
 *   toward-inf  — round away from zero
 */
double arith_round(double x);

/**
 * arith_clamp_epsilon - Return 0.0 if |@x| < configured epsilon, else @x.
 * Applied automatically by the arithmetic operations above.
 */
double arith_clamp_epsilon(double x);

#ifdef __cplusplus
}
#endif

#endif /* ARITH_H */
