/* ============================================================================
 * cf_lex.h — cliforge lexer
 *
 * Hand-rolled tokenizer for the cliforge schema language (.cf files).
 * Produces a flat array of cf_token objects from a source buffer.
 * The lexer never allocates heap memory; all tokens reference slices of
 * the caller-owned source buffer.
 *
 * Design:
 *   - Identifiers may contain hyphens: doc-title, depends-on.
 *   - Quantity literals (e.g. 4MiB, 100ms) are a single token.
 *   - Doc-comments (///, slash-star-star, slash-star-bang) are preserved.
 *   - Unrecognised characters emit CF_TOK_INVALID; the caller decides
 *     whether to abort or keep going.
 * ========================================================================= */

#ifndef CF_LEX_H
#define CF_LEX_H

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Token types
 * ---------------------------------------------------------------------- */

typedef enum cf_tok_type {
    CF_TOK_IDENT       = 0,  /* identifier or keyword (may contain '-') */
    CF_TOK_NUMBER      = 1,  /* integer or float literal                 */
    CF_TOK_QUANTITY    = 2,  /* number + unit suffix, e.g. 4MiB, 100ms  */
    CF_TOK_STRING      = 3,  /* "..." (backslash-newline continuation OK)*/
    CF_TOK_CHAR        = 4,  /* 'x'                                      */
    CF_TOK_DIRECTIVE   = 5,  /* @schema, @import                         */
    CF_TOK_DOC_COMMENT = 6,  /* doc comments: triple-slash, star-star, star-bang              */
    CF_TOK_COMMENT     = 7,  /* line and block comments                        */
    CF_TOK_LBRACE      = 8,  /* {  */
    CF_TOK_RBRACE      = 9,  /* }  */
    CF_TOK_LBRACKET    = 10, /* [  */
    CF_TOK_RBRACKET    = 11, /* ]  */
    CF_TOK_LPAREN      = 12, /* (  */
    CF_TOK_RPAREN      = 13, /* )  */
    CF_TOK_COMMA       = 14, /* ,  */
    CF_TOK_COLON       = 15, /* :  */
    CF_TOK_SEMICOLON   = 16, /* ;  */
    CF_TOK_DOT         = 17, /* .  */
    CF_TOK_ASSIGN      = 18, /* =  */
    CF_TOK_EQ          = 19, /* == */
    CF_TOK_NEQ         = 20, /* != */
    CF_TOK_RANGE       = 21, /* .. */
    CF_TOK_AND         = 22, /* && */
    CF_TOK_OR          = 23, /* || */
    CF_TOK_NOT         = 24, /* !  */
    CF_TOK_EOF         = 25,
    CF_TOK_INVALID     = 26
} cf_tok_type_t;

/* -------------------------------------------------------------------------
 * Token — a slice of the source buffer plus metadata
 * ---------------------------------------------------------------------- */

typedef struct cf_token {
    cf_tok_type_t  type;
    const char    *start;   /* pointer into source buffer (NOT NUL-terminated) */
    unsigned int   len;     /* byte length of the token text                    */
    unsigned int   line;    /* 1-based line number of token start               */
    unsigned int   col;     /* 1-based column number of token start             */
} cf_token_t;

/* -------------------------------------------------------------------------
 * Lexer state
 * ---------------------------------------------------------------------- */

#define CF_LEX_MAX_TOKENS 16384

typedef struct cf_lexer {
    const char   *src;          /* source buffer (not owned)   */
    unsigned int  src_len;      /* length of source buffer     */
    unsigned int  pos;          /* current byte offset         */
    unsigned int  line;         /* current 1-based line        */
    unsigned int  col;          /* current 1-based column      */
    const char   *filename;     /* for error messages          */

    cf_token_t    tokens[CF_LEX_MAX_TOKENS];
    unsigned int  ntokens;

    char          error[256];   /* last error message          */
} cf_lexer_t;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * cf_lex_init - Prepare a lexer for the given source buffer.
 * @lex:      Lexer state to initialise.
 * @src:      Source text (need not be NUL-terminated).
 * @src_len:  Byte length of @src.
 * @filename: Used in diagnostics only; may be NULL.
 */
void cf_lex_init(cf_lexer_t *lex, const char *src, unsigned int src_len,
                 const char *filename);

/**
 * cf_lex_run - Tokenise the entire source buffer into lex->tokens[].
 * Returns 0 on success.  On error, lex->error is set and -1 is returned;
 * partial results are still available in lex->tokens[0..lex->ntokens-1].
 */
int cf_lex_run(cf_lexer_t *lex);

/**
 * cf_tok_str - Return a human-readable name for a token type (for diagnostics).
 */
const char *cf_tok_str(cf_tok_type_t t);

/**
 * cf_tok_text - Copy token text into @buf (NUL-terminated, truncated to @bufsz).
 */
void cf_tok_text(const cf_token_t *tok, char *buf, unsigned int bufsz);

#endif /* CF_LEX_H */
