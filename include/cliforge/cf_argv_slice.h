/* ============================================================================
 * cliforge/cf_argv_slice.h  --  argv slice helper for the multi-lib pattern.
 *
 * This header is part of the cliforge SDK.  It is installed to
 *   /usr/include/cliforge/cf_argv_slice.h
 * and included in application code as:
 *   #include <cliforge/cf_argv_slice.h>
 *
 * --------------------------------------------------------------------------
 * PURPOSE
 * --------------------------------------------------------------------------
 * When a schema imports a library:
 *   @import "lib/arith.cf" as arith
 *
 * the user supplies that library's options on the command line with the
 * import alias as a namespace prefix:
 *   calctool --arith.epsilon=1e-6 --arith.div-zero=inf ...
 *
 * Each library has its own cliforge-generated parser that expects options
 * WITHOUT the prefix (just --epsilon=1e-6, --div-zero=inf).
 *
 * cf_argv_slice() scans the original argv, picks up every token that starts
 * with "--PREFIX.", strips the "PREFIX." part, and builds a new argc/argv
 * ready to pass to the library's generated *_cmdline_parse() function.
 *
 * --------------------------------------------------------------------------
 * ANALOGY
 * --------------------------------------------------------------------------
 * Think of argv as a postal bundle delivered to a mail room.
 * cf_argv_slice is the mail-room clerk who:
 *   1. pulls out every envelope addressed to "arith."
 *   2. tears off the "arith." routing label
 *   3. hands only those letters to the arith department
 * The arith department never sees mail addressed to "cmp" or "trig".
 *
 * --------------------------------------------------------------------------
 * USAGE
 * --------------------------------------------------------------------------
 *   #include <cliforge/cf_argv_slice.h>
 *
 *   char  *arith_argv[CF_SLICE_MAX_ARGS];
 *   char   arith_buf[CF_SLICE_STRBUF];
 *   int    arith_argc;
 *
 *   arith_argc = cf_argv_slice("arith",
 *                              argc, argv,
 *                              arith_argv, CF_SLICE_MAX_ARGS,
 *                              arith_buf,  CF_SLICE_STRBUF);
 *   arith_init(arith_argc, arith_argv);
 *
 * NOTE: @strbuf must outlive every use of @out_argv.  Both are typically
 *       stack-allocated at the call site, so lifetime is automatic.
 * ========================================================================= */

#ifndef CLIFORGE_CF_ARGV_SLICE_H
#define CLIFORGE_CF_ARGV_SLICE_H

#include <string.h>
#include <stdio.h>

/** Maximum number of filtered arguments returned per library. */
#define CF_SLICE_MAX_ARGS  64

/** Character pool size for the stripped argument strings. */
#define CF_SLICE_STRBUF    4096

/**
 * cf_argv_slice - Build a sub-argv for one named import alias.
 *
 * @prefix    : Import alias, e.g. "arith".  Tokens matched: --arith.*
 * @argc      : Original argc.
 * @argv      : Original argv.
 * @out_argv  : Caller-supplied pointer array, capacity >= @out_max.
 * @out_max   : Capacity of @out_argv.
 * @strbuf    : Caller-supplied character pool for the stripped strings.
 * @strbuf_sz : Size of @strbuf in bytes.
 *
 * Returns the new argc (number of entries written to @out_argv).
 * argv[0] (the program name) is always passed through as entry 0.
 *
 * Token forms handled:
 *   --PREFIX.rest=val   ->  --rest=val          (one slot, value inline)
 *   --PREFIX.rest val   ->  --rest  val          (two slots; val unchanged)
 * All other tokens are silently skipped.
 */
static int cf_argv_slice(const char *prefix,
                         int argc, char **argv,
                         char **out_argv, int out_max,
                         char *strbuf,   int strbuf_sz)
{
    char   needle[64];   /* "--PREFIX." */
    int    nlen;
    int    i, n = 0, buf_off = 0;

    (void)snprintf(needle, sizeof(needle), "--%s.", prefix);
    nlen = (int)strlen(needle);

    /* argv[0] is the program name; always pass it through. */
    if (argc > 0 && n < out_max)
        out_argv[n++] = argv[0];

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *rest;
        int         written;

        if (strncmp(arg, needle, (size_t)nlen) != 0)
            continue;           /* not for this lib */

        /* arg  = "--arith.div-zero=error"
         * rest = "div-zero=error"
         * emit = "--div-zero=error" into strbuf              */
        rest    = arg + nlen;
        written = snprintf(strbuf + buf_off,
                           (size_t)(strbuf_sz - buf_off),
                           "--%s", rest);
        if (written <= 0 || buf_off + written + 1 >= strbuf_sz)
            break;              /* strbuf full — stop processing */

        if (n < out_max)
            out_argv[n++] = strbuf + buf_off;
        buf_off += written + 1;

        /*
         * Space-separated value: --arith.epsilon 1e-6
         * Next token doesn't start with '-', treat as the value.
         * Emit it verbatim (no prefix to strip).
         */
        if (strchr(rest, '=') == NULL &&
            i + 1 < argc && argv[i + 1][0] != '-') {
            i++;
            if (n < out_max)
                out_argv[n++] = argv[i];
        }
    }

    return n;
}

#endif /* CLIFORGE_CF_ARGV_SLICE_H */
