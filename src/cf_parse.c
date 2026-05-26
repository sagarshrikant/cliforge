/* ============================================================================
 * cf_parse.c — cliforge parser
 *
 * Recursive-descent parser that builds a cf_schema_file_t from a token
 * stream.  Comments are skipped automatically.  Error recovery tries to
 * skip to the next '}' or top-level keyword so that a single bad block
 * does not prevent the rest of the file from being parsed.
 * ========================================================================= */

#include "cf_parse.h"
#include "cf_util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* -------------------------------------------------------------------------
 * Internal helpers — token stream navigation
 * ---------------------------------------------------------------------- */

/* Skip comment tokens, return current non-comment token */
static const cf_token_t *cur(const cf_parser_t *p)
{
    unsigned int i = p->pos;
    while (i < p->ntokens) {
        if (p->tokens[i].type != CF_TOK_COMMENT &&
            p->tokens[i].type != CF_TOK_DOC_COMMENT) {
            return &p->tokens[i];
        }
        i++;
    }
    return &p->tokens[p->ntokens - 1U]; /* EOF */
}

/* Advance past the current non-comment token */
static void advance(cf_parser_t *p)
{
    p->pos++;
    while (p->pos < p->ntokens) {
        if (p->tokens[p->pos].type != CF_TOK_COMMENT &&
            p->tokens[p->pos].type != CF_TOK_DOC_COMMENT) {
            return;
        }
        p->pos++;
    }
}

/* Report an error and mark parser as errored */
static void error(cf_parser_t *p, const char *msg)
{
    const cf_token_t *t = cur(p);
    cf_diag_t        *d;

    if (p->ndiags >= CF_MAX_ERRORS) return;
    d          = &p->diags[p->ndiags++];
    d->level   = CF_DIAG_ERROR;
    d->line    = t->line;
    d->col     = t->col;
    cf_strlcpy(d->filename, p->filename ? p->filename : "?",
               (unsigned int)sizeof(d->filename));
    cf_strlcpy(d->msg, msg, (unsigned int)sizeof(d->msg));
    p->had_error = 1;
}

/* Expect a specific token type; advance on match, error on mismatch */
static int expect(cf_parser_t *p, cf_tok_type_t type)
{
    char buf[64];
    const cf_token_t *t = cur(p);
    if (t->type == type) {
        advance(p);
        return 1;
    }
    (void)snprintf(buf, sizeof(buf), "expected %s, got %s",
                   cf_tok_str(type), cf_tok_str(t->type));
    error(p, buf);
    return 0;
}

/* Test whether current token matches a given type */
static int at(const cf_parser_t *p, cf_tok_type_t type)
{
    return (cur(p)->type == type) ? 1 : 0;
}

/* Test whether current IDENT token text equals @s */
static int at_kw(const cf_parser_t *p, const char *s)
{
    const cf_token_t *t = cur(p);
    if (t->type != CF_TOK_IDENT) return 0;
    return cf_str_eq_tok(s, t->start, t->len);
}

/* Copy current token text into @dst and advance */
static void take_text(cf_parser_t *p, char *dst, unsigned int dstsz)
{
    const cf_token_t *t = cur(p);
    unsigned int n = t->len;
    if (n >= dstsz) n = dstsz - 1U;
    memcpy(dst, t->start, n);
    dst[n] = '\0';
    advance(p);
}

/* Skip until RBRACE (inclusive) — used for error recovery */
static void skip_block(cf_parser_t *p)
{
    int depth = 0;
    while (!at(p, CF_TOK_EOF)) {
        if (at(p, CF_TOK_LBRACE))  { depth++; advance(p); continue; }
        if (at(p, CF_TOK_RBRACE)) {
            if (depth == 0) { advance(p); return; }
            depth--;
            advance(p);
            if (depth == 0) return; /* matched our opening brace */
            continue;
        }
        advance(p);
    }
}

/*
 * Parse a variant-keyed default block  { arm : value, ... }  and extract
 * the value of the "default :" arm into @buf.  Arms look like:
 *
 *   ifdef SYMBOL    : ident_or_string_or_(list)
 *   ifkey KEY == V  : ident_or_string_or_(list)
 *   default         : ident_or_string_or_(list)
 *
 * For v1 we only care about the "default" arm; others are silently skipped.
 * The opening '{' must be the current token when called; on return the '}'
 * has been consumed.
 */
static void parse_variant_default_block(cf_parser_t *p, char *buf,
                                        unsigned int bufsz)
{
    buf[0] = '\0';
    if (!at(p, CF_TOK_LBRACE)) return;
    advance(p); /* consume '{' */

    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        /* Detect "default :" arm */
        if (at_kw(p, "default")) {
            advance(p);                /* skip 'default' */
            if (at(p, CF_TOK_COLON)) {
                advance(p);            /* skip ':' */
                if (at(p, CF_TOK_STRING)) {
                    const cf_token_t *t = cur(p);
                    cf_unquote(buf, bufsz, t->start, t->len);
                    advance(p);
                } else if (at(p, CF_TOK_IDENT)   ||
                           at(p, CF_TOK_NUMBER)   ||
                           at(p, CF_TOK_QUANTITY)) {
                    take_text(p, buf, bufsz);
                }
                /* skip trailing '(list)' if present — value already captured */
                if (at(p, CF_TOK_LPAREN)) {
                    int d = 1;
                    advance(p);
                    while (d > 0 && !at(p, CF_TOK_EOF)) {
                        if      (at(p, CF_TOK_LPAREN)) d++;
                        else if (at(p, CF_TOK_RPAREN)) d--;
                        advance(p);
                    }
                }
            }
            continue;
        }
        /* Skip parenthesised value lists in other arms */
        if (at(p, CF_TOK_LPAREN)) {
            int d = 1;
            advance(p);
            while (d > 0 && !at(p, CF_TOK_EOF)) {
                if      (at(p, CF_TOK_LPAREN)) d++;
                else if (at(p, CF_TOK_RPAREN)) d--;
                advance(p);
            }
            continue;
        }
        advance(p);
    }
    if (at(p, CF_TOK_RBRACE)) advance(p); /* consume '}' */
}

/* -------------------------------------------------------------------------
 * Type expression parser
 * ---------------------------------------------------------------------- */

/* Parse a string-like type's optional (length=N) parameter */
static unsigned int parse_str_len(cf_parser_t *p)
{
    unsigned int result = 256U; /* default */
    char numbuf[32];

    if (!at(p, CF_TOK_LPAREN)) return result;
    advance(p); /* '(' */

    if (at_kw(p, "length")) {
        advance(p);
        if (at(p, CF_TOK_ASSIGN)) {
            advance(p);
            if (at(p, CF_TOK_NUMBER)) {
                cf_tok_text(cur(p), numbuf, sizeof(numbuf));
                result = (unsigned int)atoi(numbuf);
                advance(p);
            }
        }
    }
    if (!at(p, CF_TOK_RPAREN)) {
        error(p, "expected ')' after string length parameter");
        return result;
    }
    advance(p);
    return result;
}

static cf_base_type_t tok_to_base_type(const char *s, unsigned int len)
{
    struct { const char *name; cf_base_type_t t; } map[] = {
        {"sint8",     CF_TYPE_SINT8},
        {"sint16",    CF_TYPE_SINT16},
        {"sint32",    CF_TYPE_SINT32},
        {"sint64",    CF_TYPE_SINT64},
        {"uint8",     CF_TYPE_UINT8},
        {"uint16",    CF_TYPE_UINT16},
        {"uint32",    CF_TYPE_UINT32},
        {"uint64",    CF_TYPE_UINT64},
        {"float",     CF_TYPE_FLOAT},
        {"double",    CF_TYPE_DOUBLE},
        {"bool",      CF_TYPE_BOOL},
        {"flag",      CF_TYPE_FLAG},
        {"string",    CF_TYPE_STRING},
        {"path",      CF_TYPE_PATH},
        {"file",      CF_TYPE_FILE},
        {"dir",       CF_TYPE_DIR},
        {"duration",  CF_TYPE_DURATION},
        {"bytes",     CF_TYPE_BYTES},
        {"frequency", CF_TYPE_FREQUENCY},
        {"ratio",     CF_TYPE_RATIO},
        {NULL, CF_TYPE_NONE}
    };
    unsigned int i;
    for (i = 0U; map[i].name != NULL; i++) {
        if (cf_str_eq_tok(map[i].name, s, len)) return map[i].t;
    }
    return CF_TYPE_NONE;
}

/* Parse an inline choice type: ( A, B, C ) */
static void parse_inline_choice(cf_parser_t *p, cf_type_expr_t *expr)
{
    advance(p); /* '(' */
    expr->base     = CF_TYPE_CHOICE;
    expr->nmembers = 0U;

    while (!at(p, CF_TOK_RPAREN) && !at(p, CF_TOK_EOF)) {
        if (at(p, CF_TOK_IDENT)) {
            if (expr->nmembers < CF_MAX_MEMBERS) {
                take_text(p, expr->members[expr->nmembers],
                          CF_MAX_IDENT_LEN);
                expr->nmembers++;
            } else {
                error(p, "too many choice members");
                advance(p);
            }
        }
        if (at(p, CF_TOK_COMMA)) advance(p);
    }
    expect(p, CF_TOK_RPAREN);
}

/* Parse an inline compound type: { field: type [= default], ... } */
static void parse_inline_compound(cf_parser_t *p, cf_type_expr_t *expr)
{
    advance(p); /* '{' */
    expr->base    = CF_TYPE_COMPOUND;
    expr->nfields = 0U;

    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        cf_field_t *f;

        if (!at(p, CF_TOK_IDENT)) {
            error(p, "expected field name in compound type");
            skip_block(p);
            return;
        }
        if (expr->nfields >= CF_MAX_FIELDS) {
            error(p, "too many compound fields");
            skip_block(p);
            return;
        }
        f = &expr->fields[expr->nfields++];
        memset(f, 0, sizeof(*f));
        take_text(p, f->name, CF_MAX_IDENT_LEN);

        if (!expect(p, CF_TOK_COLON)) { skip_block(p); return; }

        /* field type */
        if (at(p, CF_TOK_IDENT)) {
            const cf_token_t *t = cur(p);
            cf_base_type_t base = tok_to_base_type(t->start, t->len);
            if (base != CF_TYPE_NONE) {
                f->base = base;
                advance(p);
                if (base == CF_TYPE_STRING || base == CF_TYPE_PATH ||
                    base == CF_TYPE_FILE   || base == CF_TYPE_DIR) {
                    f->str_len = parse_str_len(p);
                }
            } else {
                /* treat as alias */
                f->base = CF_TYPE_ALIAS;
                take_text(p, f->alias_name, CF_MAX_IDENT_LEN);
            }
        }

        /* optional in-range constraint */
        if (at_kw(p, "in")) {
            advance(p);
            /* consume range — lo .. hi */
            if (at(p, CF_TOK_NUMBER) || at(p, CF_TOK_QUANTITY)) advance(p);
            if (at(p, CF_TOK_RANGE)) advance(p);
            if (at(p, CF_TOK_NUMBER) || at(p, CF_TOK_QUANTITY)) advance(p);
        }

        /* optional default */
        if (at(p, CF_TOK_ASSIGN)) {
            advance(p);
            f->has_default = 1;
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(f->default_val, CF_MAX_IDENT_LEN,
                           t->start, t->len);
                advance(p);
            } else if (at(p, CF_TOK_IDENT)   ||
                       at(p, CF_TOK_NUMBER)   ||
                       at(p, CF_TOK_QUANTITY)) {
                take_text(p, f->default_val, CF_MAX_IDENT_LEN);
            }
        }

        if (at(p, CF_TOK_COMMA)) advance(p);
    }
    expect(p, CF_TOK_RBRACE);
}

/* Parse a type expression (everything after 'type =') */
static void parse_type_expr(cf_parser_t *p, cf_type_expr_t *expr)
{
    const cf_token_t *t;
    cf_base_type_t    base;

    memset(expr, 0, sizeof(*expr));

    if (at(p, CF_TOK_LPAREN)) {
        parse_inline_choice(p, expr);
        return;
    }

    if (at(p, CF_TOK_LBRACE)) {
        parse_inline_compound(p, expr);
        return;
    }

    if (!at(p, CF_TOK_IDENT)) {
        error(p, "expected type expression");
        return;
    }

    t    = cur(p);
    base = tok_to_base_type(t->start, t->len);

    if (base != CF_TYPE_NONE) {
        expr->base = base;
        advance(p);
        if (base == CF_TYPE_STRING || base == CF_TYPE_PATH ||
            base == CF_TYPE_FILE   || base == CF_TYPE_DIR) {
            expr->str_len = parse_str_len(p);
            if (expr->str_len == 0U) expr->str_len = 256U;
        }
        /* optional range: 'in lo..hi' */
        if (at_kw(p, "in")) {
            advance(p);
            expr->has_range = 1;
            if (at(p, CF_TOK_NUMBER) || at(p, CF_TOK_QUANTITY)) {
                take_text(p, expr->range_lo, CF_MAX_IDENT_LEN);
            }
            if (at(p, CF_TOK_RANGE)) advance(p);
            if (at(p, CF_TOK_NUMBER) || at(p, CF_TOK_QUANTITY)) {
                take_text(p, expr->range_hi, CF_MAX_IDENT_LEN);
            }
        }
    } else {
        /* named alias */
        expr->base = CF_TYPE_ALIAS;
        take_text(p, expr->alias_name, CF_MAX_IDENT_LEN);
        /* alias may be followed by a range too */
        if (at_kw(p, "in")) {
            advance(p);
            expr->has_range = 1;
            if (at(p, CF_TOK_NUMBER) || at(p, CF_TOK_QUANTITY)) {
                take_text(p, expr->range_lo, CF_MAX_IDENT_LEN);
            }
            if (at(p, CF_TOK_RANGE)) advance(p);
            if (at(p, CF_TOK_NUMBER) || at(p, CF_TOK_QUANTITY)) {
                take_text(p, expr->range_hi, CF_MAX_IDENT_LEN);
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Named type declaration:  name = ( ... ) or name = { ... }
 * Current token is the IDENT (name).
 * ---------------------------------------------------------------------- */

static void parse_named_type(cf_parser_t *p,
                              cf_named_type_t *nt)
{
    memset(nt, 0, sizeof(*nt));
    nt->line = cur(p)->line;
    take_text(p, nt->name, CF_MAX_IDENT_LEN);
    expect(p, CF_TOK_ASSIGN);
    parse_type_expr(p, &nt->expr);
}

/* Lookahead: is the current position a named-type declaration?
 * Pattern: IDENT '=' ('(' | '{') */
static int is_named_type_decl(const cf_parser_t *p)
{
    unsigned int i = p->pos;
    /* skip comments */
    while (i < p->ntokens &&
           (p->tokens[i].type == CF_TOK_COMMENT ||
            p->tokens[i].type == CF_TOK_DOC_COMMENT)) {
        i++;
    }
    if (i >= p->ntokens || p->tokens[i].type != CF_TOK_IDENT) return 0;
    i++;
    while (i < p->ntokens &&
           (p->tokens[i].type == CF_TOK_COMMENT ||
            p->tokens[i].type == CF_TOK_DOC_COMMENT)) {
        i++;
    }
    if (i >= p->ntokens || p->tokens[i].type != CF_TOK_ASSIGN) return 0;
    i++;
    while (i < p->ntokens &&
           (p->tokens[i].type == CF_TOK_COMMENT ||
            p->tokens[i].type == CF_TOK_DOC_COMMENT)) {
        i++;
    }
    if (i >= p->ntokens) return 0;
    return (p->tokens[i].type == CF_TOK_LPAREN ||
            p->tokens[i].type == CF_TOK_LBRACE) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Option qualifier block parser
 * ---------------------------------------------------------------------- */

static void parse_option_block(cf_parser_t *p, cf_option_t *opt)
{
    if (!expect(p, CF_TOK_LBRACE)) return;

    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        if (!at(p, CF_TOK_IDENT)) { advance(p); continue; }

        if (at_kw(p, "type")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            parse_type_expr(p, &opt->type);
        } else if (at_kw(p, "short")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_CHAR)) {
                const cf_token_t *t = cur(p);
                /* extract the character between single quotes */
                if (t->len >= 2U) opt->short_opt = t->start[1];
                advance(p);
            } else if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                if (t->len >= 3U) opt->short_opt = t->start[1];
                advance(p);
            } else {
                advance(p); /* skip unknown short form */
            }
        } else if (at_kw(p, "default")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            opt->has_default = 1;
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->default_val, (unsigned int)sizeof(opt->default_val),
                           t->start, t->len);
                advance(p);
            } else if (at(p, CF_TOK_LBRACE)) {
                /* variant-keyed default: extract the "default :" arm value */
                parse_variant_default_block(p, opt->default_val,
                                            (unsigned int)sizeof(opt->default_val));
            } else if (at(p, CF_TOK_IDENT) || at(p, CF_TOK_NUMBER) ||
                       at(p, CF_TOK_QUANTITY)) {
                take_text(p, opt->default_val,
                          (unsigned int)sizeof(opt->default_val));
            } else if (at(p, CF_TOK_INVALID)) {
                /* Signed numeric literal: lexer emits '-' as INVALID then NUMBER/QUANTITY.
                 * Concatenate both tokens into default_val. */
                const cf_token_t *neg = cur(p);
                if (neg->len == 1U && neg->start[0] == '-') {
                    char tmp[CF_MAX_IDENT_LEN];
                    opt->default_val[0] = '-';
                    opt->default_val[1] = '\0';
                    advance(p);
                    if (at(p, CF_TOK_NUMBER) || at(p, CF_TOK_QUANTITY)) {
                        take_text(p, tmp, sizeof(tmp));
                        cf_strlcpy(opt->default_val + 1U, tmp,
                                   (unsigned int)sizeof(opt->default_val) - 1U);
                    }
                }
            }
        } else if (at_kw(p, "required")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at_kw(p, "mandatory")) {
                opt->required = CF_REQ_MANDATORY; advance(p);
            } else {
                opt->required = CF_REQ_OPTIONAL; advance(p);
            }
        } else if (at_kw(p, "visible")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at_kw(p, "detail"))      { opt->visible = CF_VIS_DETAIL; advance(p); }
            else if (at_kw(p, "never"))  { opt->visible = CF_VIS_NEVER;  advance(p); }
            else                         { opt->visible = CF_VIS_ALL;    advance(p); }
        } else if (at_kw(p, "multiple")) {
            char numbuf[32];
            advance(p); expect(p, CF_TOK_ASSIGN);
            opt->multiple.enabled = 1;
            if (at(p, CF_TOK_NUMBER)) {
                cf_tok_text(cur(p), numbuf, sizeof(numbuf));
                opt->multiple.min = 0U;
                opt->multiple.max = (unsigned int)atoi(numbuf);
                advance(p);
                /* check for range: min..max */
                if (at(p, CF_TOK_RANGE)) {
                    advance(p);
                    opt->multiple.min = opt->multiple.max;
                    if (at(p, CF_TOK_NUMBER)) {
                        cf_tok_text(cur(p), numbuf, sizeof(numbuf));
                        opt->multiple.max = (unsigned int)atoi(numbuf);
                        advance(p);
                    }
                }
            }
        } else if (at_kw(p, "alias")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->alias_name, (unsigned int)sizeof(opt->alias_name),
                           t->start, t->len);
                advance(p);
            } else {
                take_text(p, opt->alias_name, sizeof(opt->alias_name));
            }
        } else if (at_kw(p, "depends-on")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            take_text(p, opt->depends_on, sizeof(opt->depends_on));
        } else if (at_kw(p, "conflicts")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_LBRACKET)) {
                /* list form — skip for now */
                while (!at(p, CF_TOK_RBRACKET) && !at(p, CF_TOK_EOF)) advance(p);
                advance(p);
            } else {
                take_text(p, opt->conflicts, sizeof(opt->conflicts));
            }
        } else if (at_kw(p, "sensitive")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at_kw(p, "true")) { opt->sensitive = 1; advance(p); }
            else { advance(p); }
        } else if (at_kw(p, "unique")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            advance(p); /* skip value */
        } else if (at_kw(p, "deprecated")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            opt->is_deprecated = 1;
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->deprecated, (unsigned int)sizeof(opt->deprecated),
                           t->start, t->len);
                advance(p);
            } else {
                advance(p);
            }
        } else if (at_kw(p, "display-unit")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            take_text(p, opt->display_unit, sizeof(opt->display_unit));
        } else if (at_kw(p, "help")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->help, (unsigned int)sizeof(opt->help),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
        } else if (at_kw(p, "details")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->details, (unsigned int)sizeof(opt->details),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
        } else if (at_kw(p, "note")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->note, (unsigned int)sizeof(opt->note),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
        } else if (at_kw(p, "example")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->example, (unsigned int)sizeof(opt->example),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
        } else if (at_kw(p, "since")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(opt->since, (unsigned int)sizeof(opt->since),
                           t->start, t->len);
                advance(p);
            } else {
                take_text(p, opt->since, sizeof(opt->since));
            }
        } else if (at_kw(p, "allowed")) {
            /* allowed { ... } block — skip entire block for now */
            advance(p);
            if (at(p, CF_TOK_LBRACE)) skip_block(p);
        } else {
            /* unknown qualifier — skip key = value */
            advance(p);
            if (at(p, CF_TOK_ASSIGN)) {
                advance(p);
                if (at(p, CF_TOK_LBRACE)) skip_block(p);
                else if (at(p, CF_TOK_LBRACKET)) {
                    while (!at(p, CF_TOK_RBRACKET) && !at(p, CF_TOK_EOF)) advance(p);
                    advance(p);
                } else {
                    advance(p);
                }
            }
        }
    }
    expect(p, CF_TOK_RBRACE);
}

/* -------------------------------------------------------------------------
 * Positional block parser
 * ---------------------------------------------------------------------- */

static void parse_positional_block(cf_parser_t *p, cf_positional_t *pos)
{
    char numbuf[32];

    if (!expect(p, CF_TOK_LBRACE)) return;

    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        if (!at(p, CF_TOK_IDENT)) { advance(p); continue; }

        if (at_kw(p, "type")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            parse_type_expr(p, &pos->type);
        } else if (at_kw(p, "required")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at_kw(p, "mandatory")) {
                pos->required = CF_REQ_MANDATORY; advance(p);
            } else {
                pos->required = CF_REQ_OPTIONAL; advance(p);
            }
        } else if (at_kw(p, "multiple")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            pos->multiple.enabled = 1;
            if (at(p, CF_TOK_NUMBER)) {
                cf_tok_text(cur(p), numbuf, sizeof(numbuf));
                pos->multiple.min = 0U;
                pos->multiple.max = (unsigned int)atoi(numbuf);
                advance(p);
                if (at(p, CF_TOK_RANGE)) {
                    advance(p);
                    pos->multiple.min = pos->multiple.max;
                    if (at(p, CF_TOK_NUMBER)) {
                        cf_tok_text(cur(p), numbuf, sizeof(numbuf));
                        pos->multiple.max = (unsigned int)atoi(numbuf);
                        advance(p);
                    }
                }
            }
        } else if (at_kw(p, "help")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(pos->help, (unsigned int)sizeof(pos->help),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
        } else if (at_kw(p, "details")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(pos->details, (unsigned int)sizeof(pos->details),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
        } else {
            advance(p);
            if (at(p, CF_TOK_ASSIGN)) { advance(p); advance(p); }
        }
    }
    expect(p, CF_TOK_RBRACE);
}

/* -------------------------------------------------------------------------
 * Section body parser (shared by section and subcommand)
 * ---------------------------------------------------------------------- */

static void parse_section_body(cf_parser_t *p,
                                cf_section_t *sec);

static void parse_section_body(cf_parser_t *p, cf_section_t *sec)
{
    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        if (at_kw(p, "description")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(sec->description, sizeof(sec->description),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
            continue;
        }

        /* named type declaration inside section */
        if (is_named_type_decl(p)) {
            if (sec->nnamed_types < CF_MAX_NAMED_TYPES) {
                parse_named_type(p, &sec->named_types[sec->nnamed_types]);
                sec->nnamed_types++;
            } else {
                error(p, "too many named types in section");
                advance(p);
            }
            continue;
        }

        /* option declaration */
        if (at_kw(p, "option")) {
            advance(p);
            if (sec->noptions < CF_MAX_OPTIONS) {
                cf_option_t *opt = &sec->options[sec->noptions];
                memset(opt, 0, sizeof(*opt));
                opt->line = cur(p)->line;
                if (at(p, CF_TOK_IDENT)) {
                    take_text(p, opt->name, CF_MAX_IDENT_LEN);
                }
                parse_option_block(p, opt);
                sec->noptions++;
            } else {
                error(p, "too many options in section");
                skip_block(p);
            }
            continue;
        }

        /* skip unknown content */
        advance(p);
    }
}

/* -------------------------------------------------------------------------
 * Group block parser
 * ---------------------------------------------------------------------- */

static void parse_group(cf_parser_t *p, cf_group_t *grp)
{
    memset(grp, 0, sizeof(*grp));

    if (!expect(p, CF_TOK_LBRACE)) return;

    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        if (at_kw(p, "mandatory")) {
            grp->mandatory = 1;
            advance(p);
            continue;
        }
        if (at_kw(p, "options")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (!expect(p, CF_TOK_LBRACKET)) continue;
            while (!at(p, CF_TOK_RBRACKET) && !at(p, CF_TOK_EOF)) {
                if (at(p, CF_TOK_IDENT)) {
                    if (grp->nmembers < CF_MAX_MEMBERS) {
                        take_text(p, grp->members[grp->nmembers],
                                  CF_MAX_IDENT_LEN);
                        grp->nmembers++;
                    } else {
                        advance(p);
                    }
                } else if (at(p, CF_TOK_COMMA)) {
                    advance(p);
                } else {
                    advance(p);
                }
            }
            expect(p, CF_TOK_RBRACKET);
            continue;
        }
        advance(p);
    }
    expect(p, CF_TOK_RBRACE);
}

/* -------------------------------------------------------------------------
 * Subcommand body parser
 * ---------------------------------------------------------------------- */

static void parse_subcommand_body(cf_parser_t *p, cf_subcommand_t *sub)
{
    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        if (at_kw(p, "brief")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(sub->brief, sizeof(sub->brief), t->start, t->len);
                advance(p);
            } else { advance(p); }
            continue;
        }
        if (at_kw(p, "description")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(sub->description, sizeof(sub->description),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
            continue;
        }
        if (at_kw(p, "deprecated")) {
            advance(p); expect(p, CF_TOK_ASSIGN);
            sub->is_deprecated = 1;
            if (at(p, CF_TOK_STRING)) {
                const cf_token_t *t = cur(p);
                cf_unquote(sub->deprecated, sizeof(sub->deprecated),
                           t->start, t->len);
                advance(p);
            } else { advance(p); }
            continue;
        }
        if (at_kw(p, "section")) {
            if (sub->nsections < CF_MAX_SECTIONS) {
                cf_section_t *sec = &sub->sections[sub->nsections++];
                memset(sec, 0, sizeof(*sec));
                sec->line = cur(p)->line;
                advance(p);
                if (at(p, CF_TOK_STRING)) {
                    const cf_token_t *t = cur(p);
                    cf_unquote(sec->name, sizeof(sec->name), t->start, t->len);
                    advance(p);
                }
                if (!expect(p, CF_TOK_LBRACE)) continue;
                parse_section_body(p, sec);
                expect(p, CF_TOK_RBRACE);
            } else {
                error(p, "too many sections in subcommand");
                skip_block(p);
            }
            continue;
        }
        if (at_kw(p, "option")) {
            advance(p);
            if (sub->noptions < CF_MAX_OPTIONS) {
                cf_option_t *opt = &sub->options[sub->noptions];
                memset(opt, 0, sizeof(*opt));
                opt->line = cur(p)->line;
                if (at(p, CF_TOK_IDENT)) take_text(p, opt->name, CF_MAX_IDENT_LEN);
                parse_option_block(p, opt);
                sub->noptions++;
            } else {
                error(p, "too many options in subcommand");
                skip_block(p);
            }
            continue;
        }
        if (at_kw(p, "positional")) {
            advance(p);
            if (sub->npositionals < CF_MAX_POSITIONALS) {
                cf_positional_t *pos = &sub->positionals[sub->npositionals];
                pos->line = cur(p)->line;
                if (at(p, CF_TOK_IDENT)) take_text(p, pos->name, CF_MAX_IDENT_LEN);
                parse_positional_block(p, pos);
                sub->npositionals++;
            } else {
                error(p, "too many positionals in subcommand");
                skip_block(p);
            }
            continue;
        }
        if (at_kw(p, "group")) {
            advance(p);
            if (sub->ngroups < CF_MAX_GROUPS) {
                cf_group_t *grp = &sub->groups[sub->ngroups];
                grp->line = cur(p)->line;
                if (at(p, CF_TOK_IDENT)) take_text(p, grp->name, CF_MAX_IDENT_LEN);
                parse_group(p, grp);
                sub->ngroups++;
            } else {
                error(p, "too many groups in subcommand");
                skip_block(p);
            }
            continue;
        }
        /* skip unknown content */
        advance(p);
    }
}

/* -------------------------------------------------------------------------
 * Conditional block: ifdef/ifndef/ifkey/ifnkey
 * We parse the body and merge its contents into the parent container.
 * For v1, conditions affect build-time selection only; codegen always emits.
 * ---------------------------------------------------------------------- */

typedef struct parse_ctx {
    cf_schema_file_t *file;
    cf_section_t     *section; /* may be NULL if top-level */
} parse_ctx_t;

static void parse_conditional_body(cf_parser_t *p, parse_ctx_t *ctx);

static void parse_conditional(cf_parser_t *p, parse_ctx_t *ctx)
{
    /* consume 'ifdef'|'ifndef'|'ifkey'|'ifnkey' */
    advance(p);

    /* consume condition tokens until '{' */
    while (!at(p, CF_TOK_LBRACE) && !at(p, CF_TOK_EOF) && !at(p, CF_TOK_AND) &&
           !at(p, CF_TOK_OR)) {
        advance(p);
    }
    /* skip '||' / '&&' chains */
    while (at(p, CF_TOK_AND) || at(p, CF_TOK_OR)) {
        advance(p);
        while (!at(p, CF_TOK_LBRACE) && !at(p, CF_TOK_EOF) &&
               !at(p, CF_TOK_AND) && !at(p, CF_TOK_OR)) {
            advance(p);
        }
    }

    if (!expect(p, CF_TOK_LBRACE)) return;
    parse_conditional_body(p, ctx);
    expect(p, CF_TOK_RBRACE);
}

static void parse_conditional_body(cf_parser_t *p, parse_ctx_t *ctx)
{
    cf_schema_file_t *file = ctx->file;
    cf_section_t     *sec  = ctx->section;

    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        if (at_kw(p, "ifdef") || at_kw(p, "ifndef") ||
            at_kw(p, "ifkey") || at_kw(p, "ifnkey")) {
            parse_conditional(p, ctx);
            continue;
        }
        if (at_kw(p, "section")) {
            if (file->nsections < CF_MAX_SECTIONS) {
                cf_section_t *s = &file->sections[file->nsections++];
                memset(s, 0, sizeof(*s));
                s->line = cur(p)->line;
                advance(p);
                if (at(p, CF_TOK_STRING)) {
                    const cf_token_t *t = cur(p);
                    cf_unquote(s->name, sizeof(s->name), t->start, t->len);
                    advance(p);
                }
                if (!expect(p, CF_TOK_LBRACE)) continue;
                parse_section_body(p, s);
                expect(p, CF_TOK_RBRACE);
            } else {
                skip_block(p);
            }
            continue;
        }
        if (at_kw(p, "option")) {
            cf_option_t *opt;
            advance(p);
            if (sec != NULL) {
                if (sec->noptions < CF_MAX_OPTIONS) {
                    opt = &sec->options[sec->noptions];
                    memset(opt, 0, sizeof(*opt));
                    opt->line = cur(p)->line;
                    if (at(p, CF_TOK_IDENT)) take_text(p, opt->name, CF_MAX_IDENT_LEN);
                    parse_option_block(p, opt);
                    sec->noptions++;
                } else { error(p, "too many options"); skip_block(p); }
            } else {
                if (file->noptions < CF_MAX_OPTIONS) {
                    opt = &file->options[file->noptions];
                    memset(opt, 0, sizeof(*opt));
                    opt->line = cur(p)->line;
                    if (at(p, CF_TOK_IDENT)) take_text(p, opt->name, CF_MAX_IDENT_LEN);
                    parse_option_block(p, opt);
                    file->noptions++;
                } else { error(p, "too many options"); skip_block(p); }
            }
            continue;
        }
        if (is_named_type_decl(p)) {
            cf_schema_file_t *f = file;
            if (f->nnamed_types < CF_MAX_NAMED_TYPES) {
                parse_named_type(p, &f->named_types[f->nnamed_types]);
                f->nnamed_types++;
            } else {
                error(p, "too many named types");
                advance(p); advance(p); advance(p);
            }
            continue;
        }
        advance(p);
    }
}

/* -------------------------------------------------------------------------
 * Meta block parser
 * ---------------------------------------------------------------------- */

static void parse_meta(cf_parser_t *p, cf_meta_t *meta)
{
    meta->present = 1;

    if (!expect(p, CF_TOK_LBRACE)) return;

    while (!at(p, CF_TOK_RBRACE) && !at(p, CF_TOK_EOF)) {
        if (!at(p, CF_TOK_IDENT)) { advance(p); continue; }

#define META_STR_KEY(kw, field) \
        if (at_kw(p, kw)) { \
            advance(p); expect(p, CF_TOK_ASSIGN); \
            if (at(p, CF_TOK_STRING)) { \
                const cf_token_t *t = cur(p); \
                cf_unquote(meta->field, sizeof(meta->field), t->start, t->len); \
                advance(p); \
            } else if (at(p, CF_TOK_IDENT)) { \
                take_text(p, meta->field, sizeof(meta->field)); \
            } else { advance(p); } \
            continue; \
        }

        META_STR_KEY("app",         app)
        META_STR_KEY("brief",       brief)
        META_STR_KEY("version",     version)
        META_STR_KEY("author",      author)
        META_STR_KEY("prefix",      prefix)
        META_STR_KEY("output",      output)
        META_STR_KEY("doc-title",   doc_title)
        META_STR_KEY("description", description)
        META_STR_KEY("i18n",        i18n)
#undef META_STR_KEY

        /* unknown meta key — skip */
        advance(p);
        if (at(p, CF_TOK_ASSIGN)) {
            advance(p);
            if (at(p, CF_TOK_STRING) || at(p, CF_TOK_IDENT) ||
                at(p, CF_TOK_NUMBER)) advance(p);
        }
    }
    expect(p, CF_TOK_RBRACE);
}

/* -------------------------------------------------------------------------
 * Top-level parser
 * ---------------------------------------------------------------------- */

int cf_parse(const cf_token_t *tokens, unsigned int ntokens,
             const char *filename, cf_schema_file_t *out)
{
    cf_parser_t p;
    parse_ctx_t ctx;

    memset(&p, 0, sizeof(p));
    p.tokens  = tokens;
    p.ntokens = ntokens;
    p.pos     = 0U;
    p.filename = filename;

    /* skip leading comments */
    while (p.pos < p.ntokens &&
           (p.tokens[p.pos].type == CF_TOK_COMMENT ||
            p.tokens[p.pos].type == CF_TOK_DOC_COMMENT)) {
        p.pos++;
    }

    memset(out, 0, sizeof(*out));
    if (filename) cf_strlcpy(out->filename, filename, sizeof(out->filename));

    ctx.file    = out;
    ctx.section = NULL;

    while (!at(&p, CF_TOK_EOF)) {

        /* @schema cliforge v1 */
        if (at(&p, CF_TOK_DIRECTIVE) &&
            cf_str_eq_tok("@schema", cur(&p)->start, cur(&p)->len)) {
            advance(&p);
            if (at_kw(&p, "cliforge")) advance(&p);
            if (at(&p, CF_TOK_IDENT) &&
                cf_str_eq_tok("v1", cur(&p)->start, cur(&p)->len)) {
                out->schema_version = 1U;
                advance(&p);
            }
            out->schema_present = 1;
            continue;
        }

        /* @import "path" as alias [ifkey key [== val]] */
        if (at(&p, CF_TOK_DIRECTIVE) &&
            cf_str_eq_tok("@import", cur(&p)->start, cur(&p)->len)) {
            if (out->nimports < CF_MAX_IMPORTS) {
                cf_import_t *imp = &out->imports[out->nimports];
                memset(imp, 0, sizeof(*imp));
                imp->line = cur(&p)->line;
                advance(&p);
                if (at(&p, CF_TOK_STRING)) {
                    const cf_token_t *t = cur(&p);
                    cf_unquote(imp->path, sizeof(imp->path), t->start, t->len);
                    advance(&p);
                }
                if (at_kw(&p, "as")) {
                    advance(&p);
                    take_text(&p, imp->alias, sizeof(imp->alias));
                }
                if (at_kw(&p, "ifkey") || at_kw(&p, "ifnkey")) {
                    imp->cond_kind = at_kw(&p, "ifkey")
                                   ? CF_IMPORT_COND_IFKEY
                                   : CF_IMPORT_COND_IFNKEY;
                    advance(&p);
                    take_text(&p, imp->cond_key, sizeof(imp->cond_key));
                    if (at(&p, CF_TOK_EQ) || at(&p, CF_TOK_NEQ)) {
                        cf_strlcpy(imp->cond_op,
                                   at(&p, CF_TOK_EQ) ? "==" : "!=",
                                   sizeof(imp->cond_op));
                        advance(&p);
                        take_text(&p, imp->cond_val, sizeof(imp->cond_val));
                    }
                }
                out->nimports++;
            } else {
                error(&p, "too many @import directives");
                advance(&p);
            }
            continue;
        }

        /* meta { } */
        if (at_kw(&p, "meta")) {
            advance(&p);
            parse_meta(&p, &out->meta);
            continue;
        }

        /* section "name" { } */
        if (at_kw(&p, "section")) {
            if (out->nsections < CF_MAX_SECTIONS) {
                cf_section_t *sec = &out->sections[out->nsections++];
                memset(sec, 0, sizeof(*sec));
                sec->line = cur(&p)->line;
                advance(&p);
                if (at(&p, CF_TOK_STRING)) {
                    const cf_token_t *t = cur(&p);
                    cf_unquote(sec->name, sizeof(sec->name), t->start, t->len);
                    advance(&p);
                }
                if (!expect(&p, CF_TOK_LBRACE)) continue;
                parse_section_body(&p, sec);
                expect(&p, CF_TOK_RBRACE);
            } else {
                error(&p, "too many sections");
                skip_block(&p);
            }
            continue;
        }

        /* option name { } */
        if (at_kw(&p, "option")) {
            advance(&p);
            if (out->noptions < CF_MAX_OPTIONS) {
                cf_option_t *opt = &out->options[out->noptions];
                memset(opt, 0, sizeof(*opt));
                opt->line = cur(&p)->line;
                if (at(&p, CF_TOK_IDENT)) take_text(&p, opt->name, CF_MAX_IDENT_LEN);
                parse_option_block(&p, opt);
                out->noptions++;
            } else {
                error(&p, "too many top-level options");
                skip_block(&p);
            }
            continue;
        }

        /* group name { } */
        if (at_kw(&p, "group")) {
            advance(&p);
            if (out->ngroups < CF_MAX_GROUPS) {
                cf_group_t *grp = &out->groups[out->ngroups];
                memset(grp, 0, sizeof(*grp));
                grp->line = cur(&p)->line;
                if (at(&p, CF_TOK_IDENT)) take_text(&p, grp->name, CF_MAX_IDENT_LEN);
                if (at(&p, CF_TOK_IDENT)) take_text(&p, grp->name, CF_MAX_IDENT_LEN);
                parse_group(&p, grp);
                out->ngroups++;
            } else {
                error(&p, "too many groups");
                skip_block(&p);
            }
            continue;
        }

        /* subcommand "name" { } */
        if (at_kw(&p, "subcommand")) {
            advance(&p);
            if (out->nsubcommands < CF_MAX_SUBCOMMANDS) {
                cf_subcommand_t *sub = &out->subcommands[out->nsubcommands];
                memset(sub, 0, sizeof(*sub));
                sub->line = cur(&p)->line;
                if (at(&p, CF_TOK_STRING)) {
                    const cf_token_t *t = cur(&p);
                    cf_unquote(sub->name, sizeof(sub->name), t->start, t->len);
                    advance(&p);
                } else if (at(&p, CF_TOK_IDENT)) {
                    take_text(&p, sub->name, CF_MAX_IDENT_LEN);
                }
                if (!expect(&p, CF_TOK_LBRACE)) continue;
                parse_subcommand_body(&p, sub);
                expect(&p, CF_TOK_RBRACE);
                out->nsubcommands++;
            } else {
                error(&p, "too many subcommands");
                skip_block(&p);
            }
            continue;
        }

        /* ifdef / ifndef / ifkey / ifnkey block */
        if (at_kw(&p, "ifdef") || at_kw(&p, "ifndef") ||
            at_kw(&p, "ifkey") || at_kw(&p, "ifnkey")) {
            parse_conditional(&p, &ctx);
            continue;
        }

        /* named-type declaration: choice/compound name = ... */
        if (is_named_type_decl(&p)) {
            if (out->nnamed_types < CF_MAX_NAMED_TYPES) {
                parse_named_type(&p, &out->named_types[out->nnamed_types]);
                out->nnamed_types++;
            } else {
                error(&p, "too many named types");
                advance(&p); advance(&p); advance(&p);
            }
            continue;
        }

        /* skip unknown top-level tokens */
        advance(&p);
    }

    /* Print all diagnostics accumulated during the parse */
    {
        unsigned int di;
        for (di = 0U; di < p.ndiags; di++) {
            const cf_diag_t *d = &p.diags[di];
            fprintf(stderr, "%s:%u:%u: %s: %s\n",
                    d->filename, d->line, d->col,
                    d->level == CF_DIAG_ERROR ? "error" : "warning",
                    d->msg);
        }
    }

    (void)ctx; /* suppress unused-variable warning */
    return (int)p.ndiags;
}

/* -------------------------------------------------------------------------
 * Diagnostic printer (public API)
 * ---------------------------------------------------------------------- */

void cf_parse_print_diags(const cf_parser_t *p)
{
    unsigned int i;
    for (i = 0U; i < p->ndiags; i++) {
        const cf_diag_t *d = &p->diags[i];
        fprintf(stderr, "%s:%u:%u: %s: %s\n",
                d->filename, d->line, d->col,
                d->level == CF_DIAG_ERROR ? "error" : "warning",
                d->msg);
    }
}
