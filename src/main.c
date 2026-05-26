/* ============================================================================
 * main.c - cliforge entry point
 *
 * Usage:
 *   cliforge [options] schema.cf [schema2.cf ...]
 *
 * Options:
 *   -o, --output DIR   Output directory (default: .)
 *   -v, --verbose      Verbose output
 *   -n, --dry-run      Print to stdout instead of writing files
 *   --version          Show version
 *   -h, --help         Show help
 * ========================================================================= */

#include "cf_lex.h"
#include "cf_parse.h"
#include "cf_gen.h"
#include "cf_util.h"
#include "cliforge_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void)
{
    fprintf(stdout,
        "Usage: cliforge [options] schema.cf ...\n"
        "\n"
        "Options:\n"
        "  -o, --output DIR   Output directory (default: current directory)\n"
        "  -v, --verbose      Print generated file names\n"
        "  -n, --dry-run      Print generated code to stdout, do not write files\n"
        "  --version          Show version and exit\n"
        "  -h, --help         Show this help and exit\n"
        "\n"
        "cliforge reads one or more .cf schema files and generates:\n"
        "  <output>.h   C struct definitions and function declarations\n"
        "  <output>.c   Option parser, help printer, dump function\n"
        "  <output>.md  Markdown reference chapter\n");
}

static void print_version(void)
{
    fprintf(stdout, "cliforge %s\n", CLIFORGE_VERSION_STRING);
}

int main(int argc, char *argv[])
{
    const char      *output_dir = ".";
    int              verbose    = 0;
    int              dry_run    = 0;
    int              i;
    int              nfiles     = 0;
    int              errors     = 0;

    /* --- argument parsing (hand-rolled; no irony intended) --- */
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            verbose = 1;
            continue;
        }
        if (strcmp(arg, "-n") == 0 || strcmp(arg, "--dry-run") == 0) {
            dry_run = 1;
            continue;
        }
        if ((strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) &&
            i + 1 < argc) {
            output_dir = argv[++i];
            continue;
        }
        if (strncmp(arg, "--output=", 9) == 0) {
            output_dir = arg + 9;
            continue;
        }
        if (arg[0] == '-') {
            fprintf(stderr, "cliforge: unknown option: %s\n", arg);
            return 1;
        }

        /* It's a .cf file - process it */
        {
            char             *src    = NULL;
            unsigned int      src_len = 0;
            cf_lexer_t        lex;
            cf_schema_file_t *schema = NULL;
            cf_gen_options_t  gen_opts;
            int               nerr;

            nfiles++;

            src = cf_read_file(arg, &src_len);
            if (src == NULL) {
                fprintf(stderr, "cliforge: cannot read file: %s\n", arg);
                errors++;
                continue;
            }

            /* cf_schema_file_t is large (nested fixed arrays).
             * Heap-allocate to avoid blowing the stack. */
            schema = (cf_schema_file_t *)malloc(sizeof(cf_schema_file_t));
            if (schema == NULL) {
                fprintf(stderr, "cliforge: out of memory\n");
                free(src);
                errors++;
                continue;
            }

            if (verbose) fprintf(stdout, "cliforge: parsing %s\n", arg);

            /* Lex */
            cf_lex_init(&lex, src, src_len, arg);
            if (cf_lex_run(&lex) != 0) {
                fprintf(stderr, "cliforge: lex error in %s: %s\n",
                        arg, lex.error);
                free(schema);
                free(src);
                errors++;
                continue;
            }

            /* Parse */
            nerr = cf_parse(lex.tokens, lex.ntokens, arg, schema);
            if (nerr > 0) {
                fprintf(stderr, "cliforge: %d parse error(s) in %s\n",
                        nerr, arg);
                free(schema);
                free(src);
                errors++;
                continue;
            }

            /* Generate */
            memset(&gen_opts, 0, sizeof(gen_opts));
            gen_opts.output_dir = output_dir;
            gen_opts.verbose    = verbose;
            gen_opts.dry_run    = dry_run;

            /* ---- resolve @import schemas -------------------------------- *
             * Parse each imported .cf so the generator can emit their       *
             * options in the --help output.  Path is relative to the dir    *
             * of the input file.                                             *
             * ------------------------------------------------------------ */
            {
                unsigned int ii;
                char schema_dir[512];
                const char  *slash;

                /* compute directory of the input file */
                schema_dir[0] = '\0';
                slash = strrchr(arg, '/');
#ifdef _WIN32
                {
                    const char *bslash = strrchr(arg, '\\');
                    if (bslash != NULL &&
                        (slash == NULL || bslash > slash))
                        slash = bslash;
                }
#endif
                if (slash != NULL) {
                    unsigned int dlen = (unsigned int)(slash - arg);
                    if (dlen >= (unsigned int)sizeof(schema_dir))
                        dlen = (unsigned int)sizeof(schema_dir) - 1U;
                    strncpy(schema_dir, arg, (size_t)dlen);
                    schema_dir[dlen] = '\0';
                }

                for (ii = 0U;
                     ii < schema->nimports && ii < CF_MAX_IMPORTS;
                     ii++) {
                    char              imp_path[1024]; /* schema_dir(511) + / + import_path(511) + NUL */
                    char             *isrc     = NULL;
                    unsigned int      isrc_len  = 0;
                    cf_lexer_t        ilex;
                    cf_schema_file_t *ischema   = NULL;
                    int               inerr;

                    /* build full path */
                    if (schema_dir[0] != '\0') {
                        (void)snprintf(imp_path, sizeof(imp_path),
                                       "%s/%s",
                                       schema_dir,
                                       schema->imports[ii].path);
                    } else {
                        strncpy(imp_path, schema->imports[ii].path,
                                sizeof(imp_path) - 1U);
                        imp_path[sizeof(imp_path) - 1U] = '\0';
                    }

                    isrc = cf_read_file(imp_path, &isrc_len);
                    if (isrc == NULL) {
                        fprintf(stderr,
                                "cliforge: cannot read import '%s' "
                                "(referenced from %s)\n",
                                imp_path, arg);
                        continue; /* non-fatal: skip this import */
                    }

                    ischema = (cf_schema_file_t *)malloc(
                                  sizeof(cf_schema_file_t));
                    if (ischema == NULL) {
                        fprintf(stderr,
                                "cliforge: out of memory for import %s\n",
                                imp_path);
                        free(isrc);
                        continue;
                    }

                    cf_lex_init(&ilex, isrc, isrc_len, imp_path);
                    if (cf_lex_run(&ilex) != 0) {
                        fprintf(stderr,
                                "cliforge: lex error in import %s: %s\n",
                                imp_path, ilex.error);
                        free(ischema);
                        free(isrc);
                        continue;
                    }

                    inerr = cf_parse(ilex.tokens, ilex.ntokens,
                                     imp_path, ischema);
                    if (inerr > 0) {
                        fprintf(stderr,
                                "cliforge: %d parse error(s) in import %s\n",
                                inerr, imp_path);
                        free(ischema);
                        free(isrc);
                        continue;
                    }

                    gen_opts.import_schemas[gen_opts.nimport_schemas]
                        = ischema;
                    gen_opts.import_aliases[gen_opts.nimport_schemas]
                        = schema->imports[ii].alias;
                    gen_opts.nimport_schemas++;

                    free(isrc); /* ischema freed below after cf_generate */
                }
            }

            if (cf_generate(schema, &gen_opts) != 0) {
                fprintf(stderr, "cliforge: codegen failed for %s\n", arg);
                errors++;
            }

            /* free resolved import schemas */
            {
                unsigned int ii;
                for (ii = 0U; ii < gen_opts.nimport_schemas; ii++)
                    free((void *)gen_opts.import_schemas[ii]);
            }

            free(schema);
            free(src);
        }
    }

    if (nfiles == 0) {
        fprintf(stderr, "cliforge: no input files\n");
        print_help();
        return 1;
    }

    return (errors > 0) ? 1 : 0;
}
