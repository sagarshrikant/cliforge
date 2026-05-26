/* ============================================================================
 * cf_parse.h — cliforge parser API
 * ========================================================================= */

#ifndef CF_PARSE_H
#define CF_PARSE_H

#include "cf_lex.h"
#include "cf_ast.h"

/* -------------------------------------------------------------------------
 * Diagnostic
 * ---------------------------------------------------------------------- */

typedef enum cf_diag_level {
    CF_DIAG_WARN  = 0,
    CF_DIAG_ERROR = 1
} cf_diag_level_t;

typedef struct cf_diag {
    cf_diag_level_t level;
    unsigned int    line;
    unsigned int    col;
    char            msg[512];
    char            filename[256];
} cf_diag_t;

/* -------------------------------------------------------------------------
 * Parser state
 * ---------------------------------------------------------------------- */

typedef struct cf_parser {
    const cf_token_t *tokens;     /* token array from lexer (not owned)    */
    unsigned int      ntokens;
    unsigned int      pos;        /* current token index                   */
    const char       *filename;

    cf_diag_t         diags[CF_MAX_ERRORS];
    unsigned int      ndiags;
    int               had_error;
} cf_parser_t;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * cf_parse - Parse a token stream into @out.
 * @tokens / @ntokens: output of cf_lex_run (including EOF token).
 * @filename:          used in diagnostics.
 * @out:               caller-allocated schema file node to fill.
 * Returns number of errors; 0 = clean parse.
 */
int cf_parse(const cf_token_t *tokens, unsigned int ntokens,
             const char *filename, cf_schema_file_t *out);

/**
 * cf_parse_print_diags - Print all diagnostics to stderr.
 */
void cf_parse_print_diags(const cf_parser_t *p);

#endif /* CF_PARSE_H */
