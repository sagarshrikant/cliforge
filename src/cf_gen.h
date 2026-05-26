/* ============================================================================
 * cf_gen.h -- cliforge code generator API
 * ========================================================================= */

#ifndef CF_GEN_H
#define CF_GEN_H

#include "cf_ast.h"

/**
 * cf_gen_options - Code generation configuration.
 *
 * Resolved imports: main.c parses each @import schema before calling
 * cf_generate() and stores the results in import_schemas[].  The generator
 * uses these to emit imported-lib options in the --help output.
 */
typedef struct cf_gen_options {
    const char *output_dir;   /* where to write generated files (default: ".") */
    int         dry_run;      /* 1 = print to stdout, do not write files        */
    int         verbose;

    /* Resolved @import schemas -- populated by caller before cf_generate(). */
    const cf_schema_file_t *import_schemas[CF_MAX_IMPORTS];
    const char             *import_aliases[CF_MAX_IMPORTS];
    unsigned int            nimport_schemas;
} cf_gen_options_t;

/**
 * cf_generate - Generate cmdline.h, cmdline.c, cmdline.md from a schema.
 * @file:   Parsed schema (must have meta.prefix and meta.output set).
 * @opts:   Generation options.
 * Returns 0 on success, -1 on error (message printed to stderr).
 */
int cf_generate(const cf_schema_file_t *file, const cf_gen_options_t *opts);

#endif /* CF_GEN_H */
