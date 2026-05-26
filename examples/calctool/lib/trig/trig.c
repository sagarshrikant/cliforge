/* ============================================================================
 * lib/trig/trig.c  —  Trigonometric functions for calctool.
 *
 * SHARED LIBRARY — built as -ltrig, linked at build time.
 * The argv-slice flow is identical to arith.c but the options here affect
 * angle-unit interpretation rather than arithmetic policy.
 *
 * Options parsed by generated trig_cmdline_parse():
 *   --units      enum    radians|degrees|gradians|turns   default: radians
 *   --normalize  bool    Reduce angle mod 2π first.       default: true
 *   --method     enum    taylor|cordic|lookup  (detail)   default: taylor
 * ========================================================================= */

#include "trig.h"
#include "cmdline_trig.h"   /* cliforge-generated */

#include <stdio.h>
#include <math.h>
#include <string.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define TWO_PI   (2.0 * M_PI)
#define DEG2RAD  (M_PI / 180.0)
#define GRAD2RAD (M_PI / 200.0)
#define TURN2RAD (TWO_PI)

/* ── Private state ─────────────────────────────────────────────────────── */

static struct trig_cmdline g_opts;
static int                 g_ready;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

int trig_init(int argc, char **argv)
{
    int rc;
    const char *unit_names[] = { "radians", "degrees", "gradians", "turns" };

    g_ready = 0;
    memset(&g_opts, 0, sizeof(g_opts));

    rc = trig_cmdline_parse(argc, (char *const *)argv, &g_opts);
    if (rc < 0) {
        fprintf(stderr, "trig: option parse failed\n");
        return -1;
    }

    g_ready = 1;

    fprintf(stdout,
            "[trig]  init OK: units=%s  normalize=%s\n",
            unit_names[g_opts.units],
            g_opts.normalize ? "true" : "false");
    return 0;
}

void trig_shutdown(void)
{
    g_ready = 0;
}

/* ── Unit conversion ───────────────────────────────────────────────────── */

double trig_to_radians(double angle)
{
    trig_angle_unit_t u = g_ready ? g_opts.units : TRIG_angle_unit_RADIANS;
    switch (u) {
    case TRIG_angle_unit_DEGREES:  return angle * DEG2RAD;
    case TRIG_angle_unit_GRADIANS: return angle * GRAD2RAD;
    case TRIG_angle_unit_TURNS:    return angle * TURN2RAD;
    case TRIG_angle_unit_RADIANS:
    default:                       return angle;
    }
}

double trig_from_radians(double rad)
{
    trig_angle_unit_t u = g_ready ? g_opts.units : TRIG_angle_unit_RADIANS;
    switch (u) {
    case TRIG_angle_unit_DEGREES:  return rad / DEG2RAD;
    case TRIG_angle_unit_GRADIANS: return rad / GRAD2RAD;
    case TRIG_angle_unit_TURNS:    return rad / TURN2RAD;
    case TRIG_angle_unit_RADIANS:
    default:                       return rad;
    }
}

/* Normalise an angle in radians to [0, 2π) */
static double normalize_rad(double rad)
{
    rad = fmod(rad, TWO_PI);
    if (rad < 0.0) rad += TWO_PI;
    return rad;
}

static double prepare(double angle)
{
    double rad = trig_to_radians(angle);
    if (g_ready && g_opts.normalize)
        rad = normalize_rad(rad);
    return rad;
}

/* ── Trigonometric functions ───────────────────────────────────────────── */

double trig_sin(double angle)
{
    return sin(prepare(angle));
}

double trig_cos(double angle)
{
    return cos(prepare(angle));
}

double trig_tan(double angle)
{
    return tan(prepare(angle));
}

double trig_atan2(double y, double x)
{
    /* atan2 result is a radians angle — convert to user units */
    return trig_from_radians(atan2(y, x));
}
