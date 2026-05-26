/* ===========================================================================
 *  main_dynamic.c — calctool reference main, dynamic-storage mode (v2 preview)
 *  ---------------------------------------------------------------------------
 *
 *  This file demonstrates how the same surface looks once cliforge v2 ships
 *  dynamic storage (`repeats = dynamic`, `string` without `length=`). The
 *  generator switches from static arrays to heap-backed buffers, and the
 *  consumer side must call cc_cmdline_free() to release them.
 *
 *  Until v2 lands, this file is illustrative only — it compiles against the
 *  same conceptual cmdline.h with `CLIFORGE_DYNAMIC` defined.
 *
 *  Build (future, once v2 generator emits dynamic mode):
 *      cliforge gen calctool.cf --storage=dynamic -D have-trig -D have-stats
 *      cc -std=c99 -Wall -Wextra -O2 -DCLIFORGE_DYNAMIC \
 *         main_dynamic.c cmdline.c cmdline_arith.c cmdline_cmp.c \
 *         cmdline_plug.c -ldl -o calctool-dyn
 * ========================================================================== */

#include "cmdline.h"
#include "cmdline_arith.h"
#include "cmdline_cmp.h"
#include "cmdline_plug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

/* ---------------------------------------------------------------------------
 *  In dynamic mode, repeatable records and strings become heap pointers.
 *
 *  static  mode:  struct cc_plugin plugin[16];  size_t plugin_count;
 *  dynamic mode:  struct cc_plugin *plugin;     size_t plugin_count;
 *
 *  static  mode:  char logfile[256];
 *  dynamic mode:  char *logfile;       /\* malloc'd; freed by cmdline_free() *\/
 *
 *  Generated cmdline_free() walks the struct and frees every owned buffer.
 * ------------------------------------------------------------------------- */

typedef int (*plugin_parse_fn)(int argc, char **argv, void *state);

static void die(const char *msg)
{
    fprintf(stderr, "calctool: %s\n", msg);
    exit(1);
}

int main(int argc, char **argv)
{
    /* Same parse call — the API is identical to static mode. The only
     * change is what's *inside* args after the call returns. */
    struct cc_args args;
    int rc = cc_cmdline_parse(argc, argv, &args);
    if (rc != 0) {
        die(args.cliforge_error_msg);
    }

    /* All-the-same accessors. args.plugin is now a pointer to the first of
     * args.plugin_count elements; in static mode it was a fixed array of
     * 16. The for-loop is unchanged. */
    for (size_t i = 0; i < args.plugin_count; i++) {
        printf("plugin[%zu] path=%s name=%s priority=%u\n",
               i, args.plugin[i].path, args.plugin[i].name,
               args.plugin[i].priority);
    }

    /* Quantity helpers are identical to static mode. */
    uint64_t deadline_ns = cc_duration_to_ns(&args.deadline);
    printf("deadline       : %llu ns\n", (unsigned long long)deadline_ns);

    /* … hand slices to libs, load plugins, dispatch subcommand … */

    /* Critical difference: cmdline_free is no longer a no-op. Forgetting
     * this leaks every dynamic buffer (strings, plugin array, expression
     * vector, per-alias slices, response-file expansion buffer). */
    cc_cmdline_free(&args);
    return 0;
}

/* ---------------------------------------------------------------------------
 *  Side-by-side: what changes between static and dynamic mode
 *  ---------------------------------------------------------------------------
 *
 *  Field                       static                    dynamic
 *  --------------------------- ------------------------- -----------------
 *  args.logfile                char[256]                 char *
 *  args.plugin                 struct cc_plugin[16]      struct cc_plugin *
 *  args.plugin[i].path         char[256]                 char *
 *  args.plugin_count           size_t (max 16)           size_t (unbounded)
 *  args.eval.expressions       char[32][1024]            char **
 *  args.eval.expressions_count size_t (max 32)           size_t (unbounded)
 *  cc_cmdline_free()           no-op                     releases buffers
 *
 *  Everything else — call signatures, helpers, subcommand dispatch,
 *  per-alias slicing, plugin loading — is unchanged. Switching modes is
 *  a compile-time flag (`--storage=dynamic` to the generator) plus
 *  remembering to call free.
 * ------------------------------------------------------------------------- */
