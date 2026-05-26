/* ============================================================================
 * lib/trig/trig.h  —  Trigonometric functions for calctool.
 *
 * Shared library (-ltrig).  Linked at build time; loaded by the dynamic
 * linker at process start.  Options under the "trig" prefix:
 *   --trig.units=degrees   --trig.normalize=true
 * ========================================================================= */

#ifndef TRIG_H
#define TRIG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * trig_init - Parse trig options and initialise the library.
 * @argc / @argv: filtered argv from cf_argv_slice("trig", ...).
 * Returns 0 on success, -1 on error.
 */
int trig_init(int argc, char **argv);
void trig_shutdown(void);

/* -------------------------------------------------------------------------
 * Trigonometric functions.
 * Angles are accepted in the unit configured by --trig.units.
 * All functions normalise the input to [0, 2π) when --trig.normalize=true.
 * ---------------------------------------------------------------------- */

double trig_sin(double angle);
double trig_cos(double angle);
double trig_tan(double angle);

/** trig_atan2 - Four-quadrant arctangent; result in configured units. */
double trig_atan2(double y, double x);

/** trig_deg2rad / trig_rad2deg - Unit conversion utilities. */
double trig_to_radians(double angle);
double trig_from_radians(double rad);

#ifdef __cplusplus
}
#endif

#endif /* TRIG_H */
