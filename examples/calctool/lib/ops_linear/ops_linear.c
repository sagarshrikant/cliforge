/* ============================================================================
 * lib/ops_linear/ops_linear.c  —  Linear-algebra operator plugin.
 *
 * DLOPEN PLUGIN — built as libops_linear.so.
 *
 * KEY TEACHING POINT — how a dlopen plugin uses cliforge:
 *
 *   1. The plugin author writes ops_linear.cf and runs cliforge on it.
 *      This produces cmdline_ops_linear.h / cmdline_ops_linear.c which
 *      are compiled into the .so alongside this file.
 *
 *   2. The plugin exports ONE symbol: ops_linear_init().
 *      The host app knows about this symbol by convention (shared header).
 *
 *   3. When the host loads the plugin via dlopen():
 *        handle = dlopen("libops_linear.so", RTLD_NOW);
 *        init   = dlsym(handle, "ops_linear_init");
 *
 *      It passes a filtered argc/argv for this plugin's options and a
 *      pointer to the api vtable:
 *        init(plugin_argc, plugin_argv, &ops_api);
 *
 *   4. Inside ops_linear_init(), the cliforge-generated
 *      ol_cmdline_parse() fills the private g_opts struct.
 *      The precision and backend settings then govern computation.
 *
 *   5. After init, the host calls ops via the vtable:
 *        double d = ops_api.dot(v1, v2, n);
 *
 *   Neither party needs to know how the other resolves options.
 *   The only shared contract is ops_linear.h (the vtable type and
 *   the init symbol name).
 * ========================================================================= */

#include "ops_linear.h"
#include "cmdline_ops_linear.h"   /* cliforge-generated: prefix "ol" */

#include <stdio.h>
#include <math.h>
#include <string.h>

/* ── Private state ─────────────────────────────────────────────────────── */

static struct ol_cmdline g_opts;

/* Backend index constants from the generated enum */
#define OL_BACKEND_NAIVE 0
#define OL_BACKEND_BLAS  1

/* ── Naive-C implementations ───────────────────────────────────────────── */

static double naive_dot(const double *a, const double *b, unsigned int n)
{
    double sum = 0.0;
    unsigned int i;
    for (i = 0; i < n; i++)
        sum += a[i] * b[i];
    return sum;
}

static double naive_norm(const double *v, unsigned int n)
{
    return sqrt(naive_dot(v, v, n));
}

static void naive_matvec(const double *M, unsigned int rows, unsigned int cols,
                         const double *v, double *out)
{
    unsigned int r, c;
    for (r = 0; r < rows; r++) {
        out[r] = 0.0;
        for (c = 0; c < cols; c++)
            out[r] += M[r * cols + c] * v[c];
    }
}

/* ── Plugin entry point ────────────────────────────────────────────────── */

int ops_linear_init(int argc, char **argv, ops_linear_api_t *api_out)
{
    int rc;
    const char *backend_names[] = { "naive", "blas" };

    memset(&g_opts, 0, sizeof(g_opts));

    /*
     * ol_cmdline_parse() is the cliforge-generated parser for ops_linear.cf.
     * It was generated with prefix="ol" so all symbols are prefixed "ol_".
     * The argc/argv here is already stripped of any plugin-name prefix by
     * the host's cf_argv_slice().
     */
    rc = ol_cmdline_parse(argc, (char *const *)argv, &g_opts);
    if (rc < 0) {
        fprintf(stderr, "ops_linear: option parse failed\n");
        return -1;
    }

    fprintf(stdout,
            "[ops_linear] init OK: precision=%u  backend=%s  cache=%llu bytes\n",
            (unsigned int)g_opts.precision,
            backend_names[g_opts.backend],
            (unsigned long long)g_opts.cache_size);

    /* Wire up the vtable — always use naive for this demo;
     * a real plugin would dispatch to BLAS here when backend=blas. */
    if (g_opts.backend == OL_BACKEND_BLAS) {
        fprintf(stderr,
                "[ops_linear] BLAS backend not available in this build; "
                "falling back to naive.\n");
    }

    api_out->dot     = naive_dot;
    api_out->norm    = naive_norm;
    api_out->matvec  = naive_matvec;
    api_out->version = "0.1.0";

    return 0;
}
