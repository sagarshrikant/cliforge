/* ============================================================================
 * cf_lex.c — cliforge lexer implementation
 * ========================================================================= */

#include "cf_lex.h"

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Known unit suffixes (longest-first for greedy matching)
 * ---------------------------------------------------------------------- */

static const char *const UNIT_SUFFIXES[] = {
    /* duration */
    "ns", "us", "ms",
    /* NOTE: "m" also matches minutes — we accept it after digits */
    "s", "m", "h", "d",
    /* bytes — longest first */
    "TiB", "GiB", "MiB", "KiB", "TB", "GB", "MB", "KB", "B",
    /* frequency */
    "GHz", "MHz", "kHz", "Hz",
    /* ratio */
    "%",
    NULL
};

/* -------------------------------------------------------------------------
 * Character classification helpers
 * ---------------------------------------------------------------------- */

static int is_letter(char c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
}

static int is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

static int is_hexdigit(char c)
{
    return (is_digit(c) ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'));
}

static int is_ident_part(char c)
{
    return (is_letter(c) || is_digit(c) || c == '-');
}

/* -------------------------------------------------------------------------
 * Lexer helpers
 * ---------------------------------------------------------------------- */

static char peek(const cf_lexer_t *lex, unsigned int offset)
{
    unsigned int idx = lex->pos + offset;
    return (idx < lex->src_len) ? lex->src[idx] : '\0';
}

static void advance(cf_lexer_t *lex)
{
    if (lex->pos < lex->src_len) {
        if (lex->src[lex->pos] == '\n') {
            lex->line++;
            lex->col = 1U;
        } else {
            lex->col++;
        }
        lex->pos++;
    }
}

static void push_tok(cf_lexer_t *lex, cf_tok_type_t type,
                     unsigned int start, unsigned int line, unsigned int col)
{
    cf_token_t *tok;

    if (lex->ntokens >= CF_LEX_MAX_TOKENS) {
        (void)snprintf(lex->error, sizeof(lex->error),
                       "token limit (%u) exceeded", CF_LEX_MAX_TOKENS);
        return;
    }
    tok        = &lex->tokens[lex->ntokens++];
    tok->type  = type;
    tok->start = lex->src + start;
    tok->len   = lex->pos - start;
    tok->line  = line;
    tok->col   = col;
}

/* -------------------------------------------------------------------------
 * Comment scanners
 * ---------------------------------------------------------------------- */

static void scan_line_comment(cf_lexer_t *lex)
{
    unsigned int start = lex->pos;
    unsigned int line  = lex->line;
    unsigned int col   = lex->col;
    int is_doc;

    /* peek(2) to check for '///' */
    is_doc = (lex->pos + 2 < lex->src_len && lex->src[lex->pos + 2] == '/');

    while (lex->pos < lex->src_len && lex->src[lex->pos] != '\n') {
        advance(lex);
    }
    push_tok(lex, is_doc ? CF_TOK_DOC_COMMENT : CF_TOK_COMMENT, start, line, col);
}

static void scan_block_comment(cf_lexer_t *lex)
{
    unsigned int start = lex->pos;
    unsigned int line  = lex->line;
    unsigned int col   = lex->col;
    int is_doc;
    int closed = 0;

    /* peek(2): '**' or '!*' → doc comment */
    is_doc = (lex->pos + 2 < lex->src_len &&
              (lex->src[lex->pos + 2] == '*' || lex->src[lex->pos + 2] == '!'));

    advance(lex); advance(lex); /* consume opening slash-star */

    while (lex->pos + 1 < lex->src_len) {
        if (lex->src[lex->pos] == '*' && lex->src[lex->pos + 1] == '/') {
            advance(lex); advance(lex);
            closed = 1;
            break;
        }
        advance(lex);
    }
    if (!closed) {
        push_tok(lex, CF_TOK_INVALID, start, line, col);
        return;
    }
    push_tok(lex, is_doc ? CF_TOK_DOC_COMMENT : CF_TOK_COMMENT, start, line, col);
}

/* -------------------------------------------------------------------------
 * String scanner  ("..." with backslash-newline continuation)
 * ---------------------------------------------------------------------- */

static void scan_string(cf_lexer_t *lex)
{
    unsigned int start = lex->pos;
    unsigned int line  = lex->line;
    unsigned int col   = lex->col;

    advance(lex); /* opening '"' */

    while (lex->pos < lex->src_len) {
        char c = lex->src[lex->pos];
        if (c == '\\' && lex->pos + 1 < lex->src_len) {
            advance(lex); advance(lex);
            continue;
        }
        if (c == '"') {
            advance(lex);
            push_tok(lex, CF_TOK_STRING, start, line, col);
            return;
        }
        /* bare newline (not escaped) ends the token as invalid */
        if (c == '\n') {
            push_tok(lex, CF_TOK_INVALID, start, line, col);
            return;
        }
        advance(lex);
    }
    push_tok(lex, CF_TOK_INVALID, start, line, col);
}

/* -------------------------------------------------------------------------
 * Char literal scanner  ('x' or '\n')
 * ---------------------------------------------------------------------- */

static void scan_char(cf_lexer_t *lex)
{
    unsigned int start = lex->pos;
    unsigned int line  = lex->line;
    unsigned int col   = lex->col;

    advance(lex); /* opening '\'' */

    if (lex->pos < lex->src_len && lex->src[lex->pos] == '\\') {
        advance(lex);
        if (lex->pos < lex->src_len) advance(lex);
    } else if (lex->pos < lex->src_len) {
        advance(lex);
    }

    if (lex->pos < lex->src_len && lex->src[lex->pos] == '\'') {
        advance(lex);
        push_tok(lex, CF_TOK_CHAR, start, line, col);
        return;
    }
    push_tok(lex, CF_TOK_INVALID, start, line, col);
}

/* -------------------------------------------------------------------------
 * Directive scanner  (@schema, @import)
 * ---------------------------------------------------------------------- */

static void scan_directive(cf_lexer_t *lex)
{
    unsigned int start = lex->pos;
    unsigned int line  = lex->line;
    unsigned int col   = lex->col;

    advance(lex); /* '@' */
    while (lex->pos < lex->src_len && is_ident_part(lex->src[lex->pos])) {
        advance(lex);
    }
    push_tok(lex, CF_TOK_DIRECTIVE, start, line, col);
}

/* -------------------------------------------------------------------------
 * Identifier scanner
 * ---------------------------------------------------------------------- */

static void scan_identifier(cf_lexer_t *lex)
{
    unsigned int start = lex->pos;
    unsigned int line  = lex->line;
    unsigned int col   = lex->col;

    while (lex->pos < lex->src_len && is_ident_part(lex->src[lex->pos])) {
        advance(lex);
    }
    push_tok(lex, CF_TOK_IDENT, start, line, col);
}

/* -------------------------------------------------------------------------
 * Number / Quantity scanner
 * ---------------------------------------------------------------------- */

static int match_unit_suffix(const char *p, unsigned int avail,
                              unsigned int *out_len)
{
    unsigned int i;
    for (i = 0U; UNIT_SUFFIXES[i] != NULL; i++) {
        unsigned int slen = (unsigned int)strlen(UNIT_SUFFIXES[i]);
        if (slen <= avail && strncmp(p, UNIT_SUFFIXES[i], slen) == 0) {
            /* ensure it's not followed by another ident char */
            if (slen == avail || !is_ident_part(p[slen])) {
                *out_len = slen;
                return 1;
            }
        }
    }
    /* µs special case (UTF-8: 0xC2 0xB5 followed by 's') */
    if (avail >= 3U &&
        (unsigned char)p[0] == 0xC2U &&
        (unsigned char)p[1] == 0xB5U &&
        p[2] == 's') {
        if (avail == 3U || !is_ident_part(p[3])) {
            *out_len = 3U;
            return 1;
        }
    }
    return 0;
}

static void scan_number(cf_lexer_t *lex)
{
    unsigned int start = lex->pos;
    unsigned int line  = lex->line;
    unsigned int col   = lex->col;
    unsigned int suffix_len = 0U;

    /* hex / binary / octal prefix */
    if (lex->src[lex->pos] == '0' && lex->pos + 1 < lex->src_len) {
        char next = lex->src[lex->pos + 1];
        if (next == 'x' || next == 'X') {
            advance(lex); advance(lex);
            while (lex->pos < lex->src_len &&
                   (is_hexdigit(lex->src[lex->pos]) || lex->src[lex->pos] == '_')) {
                advance(lex);
            }
            push_tok(lex, CF_TOK_NUMBER, start, line, col);
            return;
        }
        if (next == 'b' || next == 'B') {
            advance(lex); advance(lex);
            while (lex->pos < lex->src_len &&
                   (lex->src[lex->pos] == '0' || lex->src[lex->pos] == '1' ||
                    lex->src[lex->pos] == '_')) {
                advance(lex);
            }
            push_tok(lex, CF_TOK_NUMBER, start, line, col);
            return;
        }
        if (next == 'o' || next == 'O') {
            advance(lex); advance(lex);
            while (lex->pos < lex->src_len &&
                   ((lex->src[lex->pos] >= '0' && lex->src[lex->pos] <= '7') ||
                    lex->src[lex->pos] == '_')) {
                advance(lex);
            }
            push_tok(lex, CF_TOK_NUMBER, start, line, col);
            return;
        }
    }

    /* decimal integer part */
    while (lex->pos < lex->src_len &&
           (is_digit(lex->src[lex->pos]) || lex->src[lex->pos] == '_')) {
        advance(lex);
    }

    /* optional fractional part */
    if (lex->pos + 1 < lex->src_len &&
        lex->src[lex->pos] == '.' &&
        is_digit(lex->src[lex->pos + 1])) {
        advance(lex);
        while (lex->pos < lex->src_len &&
               (is_digit(lex->src[lex->pos]) || lex->src[lex->pos] == '_')) {
            advance(lex);
        }
    }

    /* optional exponent */
    if (lex->pos < lex->src_len &&
        (lex->src[lex->pos] == 'e' || lex->src[lex->pos] == 'E')) {
        advance(lex);
        if (lex->pos < lex->src_len &&
            (lex->src[lex->pos] == '+' || lex->src[lex->pos] == '-')) {
            advance(lex);
        }
        while (lex->pos < lex->src_len && is_digit(lex->src[lex->pos])) {
            advance(lex);
        }
    }

    /* try to match a unit suffix */
    if (lex->pos < lex->src_len) {
        unsigned int avail = lex->src_len - lex->pos;
        if (match_unit_suffix(lex->src + lex->pos, avail, &suffix_len)) {
            unsigned int i;
            for (i = 0U; i < suffix_len; i++) advance(lex);
            push_tok(lex, CF_TOK_QUANTITY, start, line, col);
            return;
        }
    }

    push_tok(lex, CF_TOK_NUMBER, start, line, col);
}

/* -------------------------------------------------------------------------
 * Public: cf_lex_init
 * ---------------------------------------------------------------------- */

void cf_lex_init(cf_lexer_t *lex, const char *src, unsigned int src_len,
                 const char *filename)
{
    memset(lex, 0, sizeof(*lex));
    lex->src      = src;
    lex->src_len  = src_len;
    lex->pos      = 0U;
    lex->line     = 1U;
    lex->col      = 1U;
    lex->filename = filename;
    lex->ntokens  = 0U;
}

/* -------------------------------------------------------------------------
 * Public: cf_lex_run
 * ---------------------------------------------------------------------- */

int cf_lex_run(cf_lexer_t *lex)
{
    while (lex->pos < lex->src_len) {
        char c        = lex->src[lex->pos];
        unsigned int start = lex->pos;
        unsigned int line  = lex->line;
        unsigned int col   = lex->col;

        /* Whitespace */
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
            continue;
        }

        /* Comments */
        if (c == '/' && peek(lex, 1) == '/') {
            scan_line_comment(lex);
            continue;
        }
        if (c == '/' && peek(lex, 1) == '*') {
            scan_block_comment(lex);
            continue;
        }

        /* Directives */
        if (c == '@') {
            scan_directive(lex);
            continue;
        }

        /* Strings / chars */
        if (c == '"') { scan_string(lex); continue; }
        if (c == '\'') { scan_char(lex); continue; }

        /* Numbers */
        if (is_digit(c)) {
            scan_number(lex);
            continue;
        }

        /* Identifiers */
        if (is_letter(c)) {
            scan_identifier(lex);
            continue;
        }

        /* Operators and punctuation */
        switch (c) {
        case '{': advance(lex); push_tok(lex, CF_TOK_LBRACE,   start, line, col); break;
        case '}': advance(lex); push_tok(lex, CF_TOK_RBRACE,   start, line, col); break;
        case '[': advance(lex); push_tok(lex, CF_TOK_LBRACKET, start, line, col); break;
        case ']': advance(lex); push_tok(lex, CF_TOK_RBRACKET, start, line, col); break;
        case '(': advance(lex); push_tok(lex, CF_TOK_LPAREN,   start, line, col); break;
        case ')': advance(lex); push_tok(lex, CF_TOK_RPAREN,   start, line, col); break;
        case ',': advance(lex); push_tok(lex, CF_TOK_COMMA,    start, line, col); break;
        case ':': advance(lex); push_tok(lex, CF_TOK_COLON,    start, line, col); break;
        case ';': advance(lex); push_tok(lex, CF_TOK_SEMICOLON,start, line, col); break;
        case '=':
            if (peek(lex, 1) == '=') {
                advance(lex); advance(lex);
                push_tok(lex, CF_TOK_EQ, start, line, col);
            } else {
                advance(lex);
                push_tok(lex, CF_TOK_ASSIGN, start, line, col);
            }
            break;
        case '.':
            if (peek(lex, 1) == '.') {
                advance(lex); advance(lex);
                push_tok(lex, CF_TOK_RANGE, start, line, col);
            } else {
                advance(lex);
                push_tok(lex, CF_TOK_DOT, start, line, col);
            }
            break;
        case '&':
            if (peek(lex, 1) == '&') {
                advance(lex); advance(lex);
                push_tok(lex, CF_TOK_AND, start, line, col);
            } else {
                advance(lex);
                push_tok(lex, CF_TOK_INVALID, start, line, col);
            }
            break;
        case '|':
            if (peek(lex, 1) == '|') {
                advance(lex); advance(lex);
                push_tok(lex, CF_TOK_OR, start, line, col);
            } else {
                advance(lex);
                push_tok(lex, CF_TOK_INVALID, start, line, col);
            }
            break;
        case '!':
            if (peek(lex, 1) == '=') {
                advance(lex); advance(lex);
                push_tok(lex, CF_TOK_NEQ, start, line, col);
            } else {
                advance(lex);
                push_tok(lex, CF_TOK_NOT, start, line, col);
            }
            break;
        default:
            advance(lex);
            push_tok(lex, CF_TOK_INVALID, start, line, col);
            break;
        }
    }

    /* EOF sentinel */
    if (lex->ntokens < CF_LEX_MAX_TOKENS) {
        cf_token_t *tok = &lex->tokens[lex->ntokens++];
        tok->type  = CF_TOK_EOF;
        tok->start = lex->src + lex->src_len;
        tok->len   = 0U;
        tok->line  = lex->line;
        tok->col   = lex->col;
    }

    return (lex->error[0] != '\0') ? -1 : 0;
}

/* -------------------------------------------------------------------------
 * Public: cf_tok_str
 * ---------------------------------------------------------------------- */

const char *cf_tok_str(cf_tok_type_t t)
{
    switch (t) {
    case CF_TOK_IDENT:       return "identifier";
    case CF_TOK_NUMBER:      return "number";
    case CF_TOK_QUANTITY:    return "quantity";
    case CF_TOK_STRING:      return "string";
    case CF_TOK_CHAR:        return "char";
    case CF_TOK_DIRECTIVE:   return "directive";
    case CF_TOK_DOC_COMMENT: return "doc-comment";
    case CF_TOK_COMMENT:     return "comment";
    case CF_TOK_LBRACE:      return "'{'";
    case CF_TOK_RBRACE:      return "'}'";
    case CF_TOK_LBRACKET:    return "'['";
    case CF_TOK_RBRACKET:    return "']'";
    case CF_TOK_LPAREN:      return "'('";
    case CF_TOK_RPAREN:      return "')'";
    case CF_TOK_COMMA:       return "','";
    case CF_TOK_COLON:       return "':'";
    case CF_TOK_SEMICOLON:   return "';'";
    case CF_TOK_DOT:         return "'.'";
    case CF_TOK_ASSIGN:      return "'='";
    case CF_TOK_EQ:          return "'=='";
    case CF_TOK_NEQ:         return "'!='";
    case CF_TOK_RANGE:       return "'..'";
    case CF_TOK_AND:         return "'&&'";
    case CF_TOK_OR:          return "'||'";
    case CF_TOK_NOT:         return "'!'";
    case CF_TOK_EOF:         return "end-of-file";
    case CF_TOK_INVALID:     return "invalid";
    default:                 return "unknown";
    }
}

/* -------------------------------------------------------------------------
 * Public: cf_tok_text
 * ---------------------------------------------------------------------- */

void cf_tok_text(const cf_token_t *tok, char *buf, unsigned int bufsz)
{
    unsigned int n;

    if (bufsz == 0U) return;
    n = tok->len;
    if (n >= bufsz) n = bufsz - 1U;
    memcpy(buf, tok->start, n);
    buf[n] = '\0';
}
