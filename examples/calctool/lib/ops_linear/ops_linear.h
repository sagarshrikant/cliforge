/* ============================================================================
 * lib/ops_linear/ops_linear.h  —  Linear-algebra plugin contract.
 *
 * This header is shared between:
 *   • calctool (the host)  — to call dlsym and invoke the api vtable.
 *   • ops_linear.so        — to fill the vtable in ops_linear_init().
 *
 * The host and plugin agree on this ABI privately.  cliforge has no opinion
 * on plugin ABI — it only generates the option-parsing code.
 * ========================================================================= */

#ifndef OPS_LINEAR_H
#define OPS_LINEAR_H

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Plugin API vtable.
 * The host casts a dlsym result to ops_init_fn, calls it, and then uses
 * the filled ops_linear_api_t to dispatch math operations.
 * ---------------------------------------------------------------------- */

typedef struct ops_linear_api {
    /**
     * dot - Dot product of vectors @a and @b, each of length @n.
     * Returns the scalar result.
     */
    double (*dot)(const double *a, const double *b, unsigned int n);

    /**
     * norm - Euclidean (L2) norm of vector @v of length @n.
     */
    double (*norm)(const double *v, unsigned int n);

    /**
     * matvec - Matrix-vector multiply: out = M * v.
     * @M   : row-major matrix, @rows × @cols elements.
     * @v   : input vector of length @cols.
     * @out : output vector of length @rows (caller-allocated).
     */
    void (*matvec)(const double *M, unsigned int rows, unsigned int cols,
                   const double *v, double *out);

    /** version - Null-terminated version string of the plugin. */
    const char *version;
} ops_linear_api_t;

/* -------------------------------------------------------------------------
 * Plugin entry point — exported symbol "ops_linear_init".
 *
 * @argc / @argv : argv slice produced by the host for this plugin's options.
 *                 The host strips the plugin prefix so args arrive as
 *                 --precision=12 --backend=naive (not --ops_linear.precision).
 * @api_out      : vtable to fill; caller owns the struct.
 *
 * Returns 0 on success, -1 on parse error.
 *
 * The host loads this via:
 *   typedef int (*ops_init_fn)(int, char **, ops_linear_api_t *);
 *   ops_init_fn init = (ops_init_fn)dlsym(handle, "ops_linear_init");
 * -------------------------------------------------------------------------
 */
int ops_linear_init(int argc, char **argv, ops_linear_api_t *api_out);

#ifdef __cplusplus
}
#endif

#endif /* OPS_LINEAR_H */
