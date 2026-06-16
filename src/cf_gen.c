/* ============================================================================
 * cf_gen.c — cliforge code generator
 *
 * Emits three files from a parsed cf_schema_file_t:
 *   <output>.h   — struct definitions, type enums, function declarations
 *   <output>.c   — option table, parse loop, help/dump functions
 *   <output>.md  — Markdown reference chapter
 *
 * Naming conventions:
 *   All generated C symbols are prefixed with <PREFIX>_ (uppercase).
 *   The prefix comes from meta.prefix; defaults to the app name.
 *   Hyphens in option names are mapped to underscores.
 * ========================================================================= */

#include "cf_gen.h"
#include "cf_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal context
 * ---------------------------------------------------------------------- */

typedef struct gen_ctx {
    const cf_schema_file_t *file;
    const cf_gen_options_t *opts;
    char  prefix[CF_MAX_IDENT_LEN];     /* lowercase C prefix, e.g. "cc"  */
    char  PREFIX[CF_MAX_IDENT_LEN];     /* UPPERCASE prefix, e.g. "CC"    */
    char  output[CF_MAX_IDENT_LEN];     /* base filename, e.g. "cmdline"  */
    char  app[CF_MAX_IDENT_LEN];        /* app name                       */
    FILE *fh;   /* .h output */
    FILE *fc;   /* .c output */
    FILE *fmd;  /* .md output */
    int   has_compound;                 /* any option uses compound type? */
    int   had_error;                    /* generate-time validation error */
} gen_ctx_t;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static void str_upper(char *dst, const char *src, unsigned int dstsz)
{
    unsigned int i;
    unsigned int n = (unsigned int)strlen(src);
    if (n >= dstsz) n = dstsz - 1U;
    for (i = 0U; i < n; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[i] = (c == '-') ? '_' : c;
    }
    dst[n] = '\0';
}

/* Convert option name to C identifier: hyphens → underscores */
static void opt_c_name(char *dst, const char *src, unsigned int dstsz)
{
    cf_ident_to_c(dst, src, dstsz);
}

/* Build UPPER prefixed enum name: PREFIX_OPTNAME_MEMBER */
static void enum_val(gen_ctx_t *g, const char *opt_cname,
                     const char *member, char *dst, unsigned int dstsz)
{
    char m[CF_MAX_IDENT_LEN];
    char o[CF_MAX_IDENT_LEN];
    str_upper(m, member, sizeof(m));
    str_upper(o, opt_cname, sizeof(o));
    (void)snprintf(dst, (size_t)dstsz, "%s_%s_%s", g->PREFIX, o, m);
}

/* Resolve the validation-failure policy for an option.  Returns 1 for
 * WARN (report and keep the default, continue parsing) and 0 for EXIT
 * (report and fail the parse).  Only honoured under @schema cliforge v2 —
 * v1 output is byte-for-byte unchanged. */
static int on_error_warn(const gen_ctx_t *g, const cf_option_t *opt)
{
    if (g->file->schema_version < 2U) return 0;
    if (opt != NULL && opt->on_error == CF_ONERR_WARN) return 1;
    if (opt != NULL && opt->on_error == CF_ONERR_EXIT) return 0;
    return (g->file->meta.on_error == CF_ONERR_WARN) ? 1 : 0;
}

/* Emit the trailing else-branch of a choice comparison chain, applying the
 * on-error policy.  v1 keeps the original message and always fails. */
static void emit_choice_else(gen_ctx_t *g, const cf_option_t *opt,
                             const char *long_name,
                             const char members[][CF_MAX_IDENT_LEN],
                             unsigned int nmembers)
{
    char         allowed[CF_MAX_MEMBERS * (CF_MAX_IDENT_LEN + 1U)];
    unsigned int am;

    allowed[0] = '\0';
    for (am = 0U; am < nmembers; am++) {
        if (am > 0U) {
            (void)strncat(allowed, "|",
                          sizeof(allowed) - strlen(allowed) - 1U);
        }
        (void)strncat(allowed, members[am],
                      sizeof(allowed) - strlen(allowed) - 1U);
    }

    if (g->file->schema_version >= 2U) {
        if (on_error_warn(g, opt)) {
            fprintf(g->fc,
                "\t\t\telse { fprintf(stderr, \"warning: --%s: got '%%s', "
                "allowed: %s (keeping default)\\n\", val); }\n"
                "\t\t\tcontinue;\n",
                long_name, allowed);
        } else {
            fprintf(g->fc,
                "\t\t\telse { fprintf(stderr, \"error: --%s: got '%%s', "
                "allowed: %s\\n\", val); return -1; }\n"
                "\t\t\tcontinue;\n",
                long_name, allowed);
        }
    } else {
        fprintf(g->fc,
            "\t\t\telse { fprintf(stderr, \"error: invalid value for "
            "--%s: %%s\\n\", val); return -1; }\n"
            "\t\t\tcontinue;\n",
            long_name);
    }
}

/* Emit an integer range check after the value is parsed, applying the
 * on-error policy.  v2-only; integer types only.  Avoids unsigned<0
 * comparisons (which warn) by skipping the lower bound when it is 0 on an
 * unsigned type. */
static void emit_range_check(gen_ctx_t *g, const cf_option_t *opt,
                             const char *ln, const char *path,
                             const cf_type_expr_t *expr)
{
    int is_uns;
    int skip_lo;

    if (g->file->schema_version < 2U || !expr->has_range) return;
    if (expr->range_lo[0] == '\0' || expr->range_hi[0] == '\0') return;

    is_uns  = (expr->base >= CF_TYPE_UINT8 && expr->base <= CF_TYPE_UINT64);
    skip_lo = (is_uns && strcmp(expr->range_lo, "0") == 0);

    if (on_error_warn(g, opt)) {
        if (!skip_lo) {
            fprintf(g->fc,
                "\t\t\tif (out->%s < %s) { fprintf(stderr, \"warning: --%s "
                "out of range (%s..%s), clamped\\n\"); out->%s = %s; }\n",
                path, expr->range_lo, ln, expr->range_lo, expr->range_hi,
                path, expr->range_lo);
        }
        fprintf(g->fc,
            "\t\t\tif (out->%s > %s) { fprintf(stderr, \"warning: --%s "
            "out of range (%s..%s), clamped\\n\"); out->%s = %s; }\n",
            path, expr->range_hi, ln, expr->range_lo, expr->range_hi,
            path, expr->range_hi);
    } else if (!skip_lo) {
        fprintf(g->fc,
            "\t\t\tif (out->%s < %s || out->%s > %s) { fprintf(stderr, "
            "\"error: --%s out of range (%s..%s)\\n\"); return -1; }\n",
            path, expr->range_lo, path, expr->range_hi, ln,
            expr->range_lo, expr->range_hi);
    } else {
        fprintf(g->fc,
            "\t\t\tif (out->%s > %s) { fprintf(stderr, "
            "\"error: --%s out of range (%s..%s)\\n\"); return -1; }\n",
            path, expr->range_hi, ln, expr->range_lo, expr->range_hi);
    }
}

/* Map cf_base_type to C type string */
static const char *c_type_str(cf_base_type_t t)
{
    switch (t) {
    case CF_TYPE_SINT8:     return "int8_t";
    case CF_TYPE_SINT16:    return "int16_t";
    case CF_TYPE_SINT32:    return "int32_t";
    case CF_TYPE_SINT64:    return "int64_t";
    case CF_TYPE_UINT8:     return "uint8_t";
    case CF_TYPE_UINT16:    return "uint16_t";
    case CF_TYPE_UINT32:    return "uint32_t";
    case CF_TYPE_UINT64:    return "uint64_t";
    case CF_TYPE_FLOAT:     return "float";
    case CF_TYPE_DOUBLE:    return "double";
    case CF_TYPE_BOOL:      return "int";
    case CF_TYPE_FLAG:      return "int";
    default:                return "int";
    }
}

/* Find a named type in the schema by name */
static const cf_named_type_t *find_named_type(const cf_schema_file_t *f,
                                               const char *name)
{
    unsigned int i;
    for (i = 0U; i < f->nnamed_types; i++) {
        if (strcmp(f->named_types[i].name, name) == 0) return &f->named_types[i];
    }
    /* also search inside sections */
    for (i = 0U; i < f->nsections; i++) {
        unsigned int j;
        for (j = 0U; j < f->sections[i].nnamed_types; j++) {
            if (strcmp(f->sections[i].named_types[j].name, name) == 0) {
                return &f->sections[i].named_types[j];
            }
        }
    }
    return NULL;
}

/* Resolve an alias type to its base (chase one level) */
static const cf_type_expr_t *resolve_alias(const cf_schema_file_t *f,
                                            const cf_type_expr_t *expr)
{
    const cf_named_type_t *nt;
    if (expr->base != CF_TYPE_ALIAS) return expr;
    nt = find_named_type(f, expr->alias_name);
    if (nt == NULL) return expr;
    return &nt->expr;
}

/* =========================================================================
 * Header generation
 * ====================================================================== */

/* =====================================================================
 *  Unit-aware quantity types (v2)
 *
 *  For each quantity group actually used by the schema we emit a typed
 *  { value, unit } struct, a unit enum, and a conversion helper to the
 *  group's base unit.  v1 output is unchanged (plain uint64_t).
 * ===================================================================== */
typedef struct {
    cf_base_type_t base;
    const char    *gname;       /* "duration"            */
    const char    *to_suffix;   /* helper name suffix: "ns"/"bytes"/"hz" */
    const char    *units[10];   /* accepted suffixes, NULL terminated    */
    const char    *mults[10];   /* multiplier to base unit (uint64 expr) */
} cf_qgroup_t;

static const cf_qgroup_t CF_QGROUPS[] = {
    { CF_TYPE_DURATION, "duration", "ns",
      { "ns","us","ms","s","m","h","d", NULL },
      { "1UL","1000UL","1000000UL","1000000000UL",
        "60000000000UL","3600000000000UL","86400000000000UL", NULL } },
    { CF_TYPE_BYTES, "bytes", "bytes",
      { "B","KB","KiB","MB","MiB","GB","GiB","TB","TiB", NULL },
      { "1UL","1000UL","1024UL","1000000UL","1048576UL","1000000000UL",
        "1073741824UL","1000000000000UL","1099511627776UL", NULL } },
    { CF_TYPE_FREQUENCY, "frequency", "hz",
      { "Hz","kHz","MHz","GHz", NULL },
      { "1UL","1000UL","1000000UL","1000000000UL", NULL } }
};
#define CF_NQGROUPS (sizeof(CF_QGROUPS) / sizeof(CF_QGROUPS[0]))

/* Is a quantity group used anywhere (top-level + section options, and
 * compound fields)?  Determines which typed structs/helpers to emit. */
static int quantity_used(const cf_schema_file_t *f, cf_base_type_t base)
{
    unsigned int i, j, k;
    for (i = 0U; i < f->noptions; i++)
        if (f->options[i].type.base == base) return 1;
    for (i = 0U; i < f->nsections; i++) {
        const cf_section_t *sec = &f->sections[i];
        for (j = 0U; j < sec->noptions; j++)
            if (sec->options[j].type.base == base) return 1;
    }
    /* compound fields within named types (top-level + section) */
    for (i = 0U; i < f->nnamed_types; i++) {
        const cf_type_expr_t *e = &f->named_types[i].expr;
        for (k = 0U; k < e->nfields; k++)
            if (e->fields[k].base == base) return 1;
    }
    for (i = 0U; i < f->nsections; i++) {
        const cf_section_t *sec = &f->sections[i];
        for (j = 0U; j < sec->nnamed_types; j++) {
            const cf_type_expr_t *e = &sec->named_types[j].expr;
            for (k = 0U; k < e->nfields; k++)
                if (e->fields[k].base == base) return 1;
        }
    }
    return 0;
}

/* Look up the quantity group descriptor for a base type, or NULL. */
static const cf_qgroup_t *qgroup_for(cf_base_type_t base)
{
    unsigned int gi;
    for (gi = 0U; gi < CF_NQGROUPS; gi++)
        if (CF_QGROUPS[gi].base == base) return &CF_QGROUPS[gi];
    return NULL;
}

/* Emit all named choice / compound type definitions */
static void gen_named_types(gen_ctx_t *g, const cf_named_type_t *types,
                             unsigned int n)
{
    unsigned int i;

    for (i = 0U; i < n; i++) {
        const cf_named_type_t *nt = &types[i];
        char cname[CF_MAX_IDENT_LEN];
        opt_c_name(cname, nt->name, sizeof(cname));

        if (nt->expr.base == CF_TYPE_CHOICE) {
            unsigned int m;
            fprintf(g->fh, "typedef enum %s_%s {\n", g->prefix, cname);
            for (m = 0U; m < nt->expr.nmembers; m++) {
                char eval[CF_MAX_IDENT_LEN * 3];
                enum_val(g, cname, nt->expr.members[m], eval, sizeof(eval));
                fprintf(g->fh, "\t%s = %u", eval, m);
                if (m + 1U < nt->expr.nmembers) fprintf(g->fh, ",");
                fprintf(g->fh, "\n");
            }
            fprintf(g->fh, "} %s_%s_t;\n\n", g->prefix, cname);
        }

        if (nt->expr.base == CF_TYPE_COMPOUND) {
            unsigned int fi;
            fprintf(g->fh, "typedef struct %s_%s {\n", g->prefix, cname);
            for (fi = 0U; fi < nt->expr.nfields; fi++) {
                const cf_field_t *fld = &nt->expr.fields[fi];
                char fcname[CF_MAX_IDENT_LEN];
                opt_c_name(fcname, fld->name, sizeof(fcname));

                if (fld->base == CF_TYPE_STRING || fld->base == CF_TYPE_PATH ||
                    fld->base == CF_TYPE_FILE   || fld->base == CF_TYPE_DIR) {
                    unsigned int slen = (fld->str_len > 0U) ? fld->str_len : 256U;
                    fprintf(g->fh, "\tchar\t%s[%u];\n", fcname, slen);
                } else if (fld->base == CF_TYPE_ALIAS) {
                    char acname[CF_MAX_IDENT_LEN];
                    opt_c_name(acname, fld->alias_name, sizeof(acname));
                    fprintf(g->fh, "\t%s_%s_t\t%s;\n",
                            g->prefix, acname, fcname);
                } else if (g->file->schema_version >= 2U &&
                           qgroup_for(fld->base) != NULL) {
                    /* v2: typed quantity field { value, unit } */
                    const cf_qgroup_t *fqg = qgroup_for(fld->base);
                    fprintf(g->fh, "\tstruct %s_%s\t%s;\n",
                            g->prefix, fqg->gname, fcname);
                } else if (g->file->schema_version >= 2U &&
                           fld->base != CF_TYPE_RATIO) {
                    /* v2: numeric / bool / float fields get their real C type */
                    fprintf(g->fh, "\t%s\t%s;\n",
                            c_type_str(fld->base), fcname);
                } else {
                    /* v1 (and ratio): char[] buffer, app converts */
                    unsigned int nslen = (fld->str_len > 0U) ? fld->str_len : 64U;
                    fprintf(g->fh, "\tchar\t%s[%u];\n", fcname, nslen);
                }
            }
            fprintf(g->fh, "} %s_%s_t;\n\n", g->prefix, cname);
        }
    }
}


/* Emit a field declaration for one option inside the cmdline struct */
static void emit_option_field(gen_ctx_t *g, const cf_option_t *opt,
                               const char *indent)
{
    char cname[CF_MAX_IDENT_LEN];
    const cf_type_expr_t *expr;

    if (opt->name[0] == '\0') return;
    opt_c_name(cname, opt->name, sizeof(cname));
    expr = resolve_alias(g->file, &opt->type);

    if (opt->multiple.enabled && opt->multiple.max > 1U) {
        unsigned int maxn = opt->multiple.max;
        /* emit array + count */
        if (expr->base == CF_TYPE_STRING || expr->base == CF_TYPE_PATH ||
            expr->base == CF_TYPE_FILE   || expr->base == CF_TYPE_DIR) {
            unsigned int slen = (expr->str_len > 0U) ? expr->str_len : 256U;
            fprintf(g->fh, "%schar\t%s[%u][%u];\n", indent, cname, maxn, slen);
        } else if (expr->base == CF_TYPE_CHOICE) {
            /* inline choice in multiple — rare but handle it */
            fprintf(g->fh, "%sint\t%s[%u];\n", indent, cname, maxn);
        } else if (expr->base == CF_TYPE_COMPOUND) {
            /* compound via alias: emit cc_alias_t name[N] */
            const cf_type_expr_t *orig = &opt->type;
            if (orig->base == CF_TYPE_ALIAS) {
                char acname[CF_MAX_IDENT_LEN];
                opt_c_name(acname, orig->alias_name, sizeof(acname));
                fprintf(g->fh, "%sstruct %s_%s\t%s[%u];\n",
                        indent, g->prefix, acname, cname, maxn);
            } else {
                /* inline compound multiple: emit int array as placeholder */
                fprintf(g->fh, "%sint\t%s[%u]; /* TODO: inline compound */\n",
                        indent, cname, maxn);
            }
        } else if (expr->base == CF_TYPE_ALIAS) {
            char acname[CF_MAX_IDENT_LEN];
            const cf_named_type_t *nt;
            opt_c_name(acname, expr->alias_name, sizeof(acname));
            nt = find_named_type(g->file, expr->alias_name);
            if (nt != NULL && nt->expr.base == CF_TYPE_COMPOUND) {
                fprintf(g->fh, "%sstruct %s_%s\t%s[%u];\n",
                        indent, g->prefix, acname, cname, maxn);
            } else {
                fprintf(g->fh, "%s%s_%s_t\t%s[%u];\n",
                        indent, g->prefix, acname, cname, maxn);
            }
        } else {
            fprintf(g->fh, "%s%s\t%s[%u];\n",
                    indent, c_type_str(expr->base), cname, maxn);
        }
        fprintf(g->fh, "%sint\t%s_count;\n", indent, cname);
        return;
    }

    /* single-value fields */
    if (expr->base == CF_TYPE_STRING || expr->base == CF_TYPE_PATH ||
        expr->base == CF_TYPE_FILE   || expr->base == CF_TYPE_DIR) {
        unsigned int slen = (expr->str_len > 0U) ? expr->str_len : 256U;
        fprintf(g->fh, "%schar\t%s[%u];\n", indent, cname, slen);
    } else if (expr->base == CF_TYPE_FLAG || expr->base == CF_TYPE_BOOL) {
        fprintf(g->fh, "%sint\t%s;\n", indent, cname);
    } else if (expr->base == CF_TYPE_CHOICE) {
        /* use the named type enum if available */
        if (opt->type.base == CF_TYPE_ALIAS) {
            char acname[CF_MAX_IDENT_LEN];
            opt_c_name(acname, opt->type.alias_name, sizeof(acname));
            fprintf(g->fh, "%s%s_%s_t\t%s;\n", indent, g->prefix, acname, cname);
        } else {
            fprintf(g->fh, "%sint\t%s;\n", indent, cname);
        }
    } else if (expr->base == CF_TYPE_COMPOUND &&
               g->file->schema_version >= 2U &&
               opt->type.base == CF_TYPE_ALIAS) {
        /* v2: a named compound option references its typed typedef so the
         * fields keep their real types (int/struct/enum). */
        char acname[CF_MAX_IDENT_LEN];
        opt_c_name(acname, opt->type.alias_name, sizeof(acname));
        fprintf(g->fh, "%sstruct %s_%s\t%s;\n", indent, g->prefix, acname, cname);
    } else if (expr->base == CF_TYPE_COMPOUND) {
        /* inline compound (or v1) — emit anonymous struct */
        unsigned int fi;
        fprintf(g->fh, "%sstruct {\n", indent);
        for (fi = 0U; fi < expr->nfields; fi++) {
            const cf_field_t *fld = &expr->fields[fi];
            char fcname[CF_MAX_IDENT_LEN];
            opt_c_name(fcname, fld->name, sizeof(fcname));
            if (fld->base == CF_TYPE_STRING || fld->base == CF_TYPE_PATH ||
                fld->base == CF_TYPE_FILE   || fld->base == CF_TYPE_DIR) {
                unsigned int slen = (fld->str_len > 0U) ? fld->str_len : 256U;
                fprintf(g->fh, "%s\tchar\t%s[%u];\n", indent, fcname, slen);
            } else {
                fprintf(g->fh, "%s\t%s\t%s;\n", indent,
                        c_type_str(fld->base), fcname);
            }
        }
        fprintf(g->fh, "%s} %s;\n", indent, cname);
    } else if (expr->base == CF_TYPE_ALIAS) {
        char acname[CF_MAX_IDENT_LEN];
        const cf_named_type_t *nt;
        opt_c_name(acname, expr->alias_name, sizeof(acname));
        nt = find_named_type(g->file, expr->alias_name);
        if (nt != NULL && nt->expr.base == CF_TYPE_COMPOUND) {
            fprintf(g->fh, "%sstruct %s_%s\t%s;\n",
                    indent, g->prefix, acname, cname);
        } else {
            fprintf(g->fh, "%s%s_%s_t\t%s;\n",
                    indent, g->prefix, acname, cname);
        }
    } else if (expr->base == CF_TYPE_DURATION || expr->base == CF_TYPE_BYTES ||
               expr->base == CF_TYPE_FREQUENCY || expr->base == CF_TYPE_RATIO) {
        const cf_qgroup_t *qg = qgroup_for(expr->base);
        if (g->file->schema_version >= 2U && qg != NULL) {
            fprintf(g->fh, "%sstruct %s_%s\t%s;\n",
                    indent, g->prefix, qg->gname, cname);
        } else {
            fprintf(g->fh, "%suint64_t\t%s;\n", indent, cname);
        }
    } else {
        fprintf(g->fh, "%s%s\t%s;\n", indent, c_type_str(expr->base), cname);
    }
}

/* Header declarations: enum + struct + conversion-helper prototype. */
static void gen_quantity_decls(gen_ctx_t *g)
{
    unsigned int gi, ui;
    char gup[CF_MAX_IDENT_LEN];

    if (g->file->schema_version < 2U) return;

    for (gi = 0U; gi < CF_NQGROUPS; gi++) {
        const cf_qgroup_t *q = &CF_QGROUPS[gi];
        if (!quantity_used(g->file, q->base)) continue;
        str_upper(gup, q->gname, sizeof(gup));

        fprintf(g->fh, "enum %s_%s_unit {\n", g->prefix, q->gname);
        for (ui = 0U; q->units[ui] != NULL; ui++) {
            char u[16];
            str_upper(u, q->units[ui], sizeof(u));
            fprintf(g->fh, "\t%s_%s_%s%s\n",
                    g->PREFIX, gup, u, (q->units[ui + 1U] != NULL) ? "," : "");
        }
        fprintf(g->fh, "};\n");
        fprintf(g->fh,
                "struct %s_%s { uint64_t value; enum %s_%s_unit unit; };\n",
                g->prefix, q->gname, g->prefix, q->gname);
        fprintf(g->fh,
                "uint64_t %s_%s_to_%s(const struct %s_%s *q);\n\n",
                g->prefix, q->gname, q->to_suffix, g->prefix, q->gname);
    }
}

/* Source definitions: a parse function (value+suffix -> struct) and the
 * base-unit conversion helper, per used group. */
static void gen_quantity_defs(gen_ctx_t *g)
{
    unsigned int gi, ui;
    char gup[CF_MAX_IDENT_LEN];

    if (g->file->schema_version < 2U) return;

    for (gi = 0U; gi < CF_NQGROUPS; gi++) {
        const cf_qgroup_t *q = &CF_QGROUPS[gi];
        if (!quantity_used(g->file, q->base)) continue;
        str_upper(gup, q->gname, sizeof(gup));

        /* parser: read magnitude, then match the unit suffix */
        fprintf(g->fc,
            "static int cf__parse_%s(const char *s, struct %s_%s *o)\n{\n"
            "\tchar *e;\n"
            "\to->value = (uint64_t)strtoul(s, &e, 10);\n",
            q->gname, g->prefix, q->gname);
        for (ui = 0U; q->units[ui] != NULL; ui++) {
            char u[16];
            str_upper(u, q->units[ui], sizeof(u));
            fprintf(g->fc,
                "\t%sif (strcmp(e, \"%s\") == 0) o->unit = %s_%s_%s;\n",
                (ui == 0U) ? "" : "else ",
                q->units[ui], g->PREFIX, gup, u);
        }
        fprintf(g->fc, "\telse return -1;\n\treturn 0;\n}\n\n");

        /* converter to base unit */
        fprintf(g->fc,
            "uint64_t %s_%s_to_%s(const struct %s_%s *q)\n{\n\tswitch (q->unit) {\n",
            g->prefix, q->gname, q->to_suffix, g->prefix, q->gname);
        for (ui = 0U; q->units[ui] != NULL; ui++) {
            char u[16];
            str_upper(u, q->units[ui], sizeof(u));
            fprintf(g->fc,
                "\tcase %s_%s_%s: return q->value * %s;\n",
                g->PREFIX, gup, u, q->mults[ui]);
        }
        fprintf(g->fc, "\tdefault: return q->value;\n\t}\n}\n\n");
    }
}

static void gen_header(gen_ctx_t *g)
{
    char guard[CF_MAX_IDENT_LEN * 2];
    unsigned int i;
    unsigned int si;

    /* Include guard */
    (void)snprintf(guard, sizeof(guard), "%s_CMDLINE_H", g->PREFIX);
    fprintf(g->fh,
            "/* Generated by cliforge — DO NOT EDIT */\n"
            "#ifndef %s\n"
            "#define %s\n\n"
            "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
            "#include <stdint.h>\n\n",
            guard, guard);

    /* Subcommand enum (if subcommands exist) */
    if (g->file->nsubcommands > 0U) {
        fprintf(g->fh, "typedef enum %s_subcmd {\n", g->prefix);
        fprintf(g->fh, "\t%s_CMD_NONE = 0", g->PREFIX);
        for (i = 0U; i < g->file->nsubcommands; i++) {
            char scname[CF_MAX_IDENT_LEN];
            str_upper(scname, g->file->subcommands[i].name, sizeof(scname));
            fprintf(g->fh, ",\n\t%s_CMD_%s = %u",
                    g->PREFIX, scname, i + 1U);
        }
        fprintf(g->fh, "\n} %s_subcmd_t;\n\n", g->prefix);
    }

    /* Quantity structs first — named compound typedefs may embed them. */
    gen_quantity_decls(g);

    /* Named types from top-level and all sections */
    gen_named_types(g, g->file->named_types, g->file->nnamed_types);
    for (si = 0U; si < g->file->nsections; si++) {
        gen_named_types(g, g->file->sections[si].named_types,
                        g->file->sections[si].nnamed_types);
    }

    /* Main struct */
    fprintf(g->fh, "struct %s_cmdline {\n", g->prefix);

    /* subcommand selector */
    if (g->file->nsubcommands > 0U) {
        fprintf(g->fh, "\t%s_subcmd_t subcmd;\n", g->prefix);
    }

    /* top-level options */
    for (i = 0U; i < g->file->noptions; i++) {
        emit_option_field(g, &g->file->options[i], "\t");
    }

    /* options from sections */
    for (si = 0U; si < g->file->nsections; si++) {
        unsigned int oi;
        for (oi = 0U; oi < g->file->sections[si].noptions; oi++) {
            emit_option_field(g, &g->file->sections[si].options[oi], "\t");
        }
    }

    /* per-subcommand sub-structs */
    for (i = 0U; i < g->file->nsubcommands; i++) {
        const cf_subcommand_t *sub = &g->file->subcommands[i];
        char scname[CF_MAX_IDENT_LEN];
        unsigned int oi;
        unsigned int pi;
        unsigned int ssi;

        opt_c_name(scname, sub->name, sizeof(scname));
        fprintf(g->fh, "\tstruct {\n");

        for (oi = 0U; oi < sub->noptions; oi++) {
            emit_option_field(g, &sub->options[oi], "\t\t");
        }
        for (ssi = 0U; ssi < sub->nsections; ssi++) {
            unsigned int soi;
            for (soi = 0U; soi < sub->sections[ssi].noptions; soi++) {
                emit_option_field(g, &sub->sections[ssi].options[soi], "\t\t");
            }
        }
        /* positionals */
        for (pi = 0U; pi < sub->npositionals; pi++) {
            const cf_positional_t *pos = &sub->positionals[pi];
            char pcname[CF_MAX_IDENT_LEN];
            const cf_type_expr_t *expr = resolve_alias(g->file, &pos->type);
            opt_c_name(pcname, pos->name, sizeof(pcname));
            if (pos->multiple.enabled && pos->multiple.max > 1U) {
                unsigned int maxn = pos->multiple.max;
                if (expr->base == CF_TYPE_STRING || expr->base == CF_TYPE_PATH ||
                    expr->base == CF_TYPE_FILE   || expr->base == CF_TYPE_DIR) {
                    unsigned int slen = (expr->str_len > 0U) ? expr->str_len : 256U;
                    fprintf(g->fh, "\t\tchar\t%s[%u][%u];\n", pcname, maxn, slen);
                } else {
                    fprintf(g->fh, "\t\t%s\t%s[%u];\n",
                            c_type_str(expr->base), pcname, maxn);
                }
                fprintf(g->fh, "\t\tint\t%s_count;\n", pcname);
            } else {
                if (expr->base == CF_TYPE_STRING || expr->base == CF_TYPE_PATH ||
                    expr->base == CF_TYPE_FILE   || expr->base == CF_TYPE_DIR) {
                    unsigned int slen = (expr->str_len > 0U) ? expr->str_len : 256U;
                    fprintf(g->fh, "\t\tchar\t%s[%u];\n", pcname, slen);
                } else {
                    fprintf(g->fh, "\t\t%s\t%s;\n",
                            c_type_str(expr->base), pcname);
                }
            }
        }
        fprintf(g->fh, "\t} %s;\n", scname);
    }

    fprintf(g->fh, "};\n\n");

    /* Function declarations */
    fprintf(g->fh,
            "int  %s_cmdline_parse(int argc, char *const *argv,\n"
            "                      struct %s_cmdline *out);\n"
            "void %s_cmdline_help(void);\n"
            "void %s_cmdline_version(void);\n"
            "void %s_cmdline_dump(const struct %s_cmdline *args);\n\n",
            g->prefix, g->prefix, g->prefix, g->prefix, g->prefix, g->prefix);

    fprintf(g->fh,
            "#ifdef __cplusplus\n} /* extern \"C\" */\n#endif\n\n"
            "#endif /* %s */\n", guard);
}

/* =========================================================================
 * .c file generation — option table + parse loop
 * ====================================================================== */

/* Collect all options into a flat list for the parse loop */
typedef struct flat_opt {
    char             long_name[CF_MAX_IDENT_LEN]; /* e.g. "verbose"     */
    char             c_name[CF_MAX_IDENT_LEN];    /* e.g. "verbose"     */
    char             struct_path[CF_MAX_IDENT_LEN * 2]; /* e.g. "eval.echo" */
    char             short_opt;
    const cf_option_t *opt;
    const char       *subcmd; /* NULL = top-level */
} flat_opt_t;

#define MAX_FLAT_OPTS 512

typedef struct flat_list {
    flat_opt_t entries[MAX_FLAT_OPTS];
    unsigned int n;
} flat_list_t;

static void flat_add(flat_list_t *fl, const cf_option_t *opt,
                     const char *subcmd_cname)
{
    flat_opt_t *e;
    if (fl->n >= MAX_FLAT_OPTS) return;
    e = &fl->entries[fl->n++];
    cf_strlcpy(e->long_name, opt->name, sizeof(e->long_name));
    opt_c_name(e->c_name, opt->name, sizeof(e->c_name));
    if (subcmd_cname != NULL) {
        /* Copy c_name to a local buffer before snprintf to avoid a GCC
         * -Wrestrict false positive: both e->struct_path and e->c_name
         * live inside the same flat_opt_t, so GCC's conservative alias
         * analysis incorrectly suspects overlap with the destination. */
        char tmp_cname[CF_MAX_IDENT_LEN];
        cf_strlcpy(tmp_cname, e->c_name, sizeof(tmp_cname));
        (void)snprintf(e->struct_path, sizeof(e->struct_path),
                       "%s.%s", subcmd_cname, tmp_cname);
        e->subcmd = subcmd_cname;
    } else {
        cf_strlcpy(e->struct_path, e->c_name, sizeof(e->struct_path));
        e->subcmd = NULL;
    }
    e->short_opt = opt->short_opt;
    e->opt       = opt;
}

static void collect_options(gen_ctx_t *g, flat_list_t *fl)
{
    unsigned int i;
    unsigned int si;

    /* top-level options */
    for (i = 0U; i < g->file->noptions; i++) flat_add(fl, &g->file->options[i], NULL);

    /* section options */
    for (si = 0U; si < g->file->nsections; si++) {
        unsigned int oi;
        for (oi = 0U; oi < g->file->sections[si].noptions; oi++) {
            flat_add(fl, &g->file->sections[si].options[oi], NULL);
        }
    }
}

/* Emit the default value initialisation for one option */
static void emit_default(gen_ctx_t *g, const flat_opt_t *e)
{
    const cf_option_t    *opt  = e->opt;
    const cf_type_expr_t *expr = resolve_alias(g->file, &opt->type);

    if (!opt->has_default) return;

    if (expr->base == CF_TYPE_FLAG || expr->base == CF_TYPE_BOOL) {
        int val = (strcmp(opt->default_val, "true") == 0 ||
                   strcmp(opt->default_val, "1") == 0) ? 1 : 0;
        fprintf(g->fc, "\tout->%s = %d;\n", e->struct_path, val);
    } else if (expr->base == CF_TYPE_STRING || expr->base == CF_TYPE_PATH ||
               expr->base == CF_TYPE_FILE   || expr->base == CF_TYPE_DIR) {
        fprintf(g->fc, "\t(void)strncpy(out->%s, \"%s\", sizeof(out->%s) - 1U);\n",
                e->struct_path, opt->default_val, e->struct_path);
    } else if (expr->base == CF_TYPE_ALIAS) {
        /* look up the alias to see if it's a choice */
        const cf_named_type_t *nt = find_named_type(g->file, expr->alias_name);
        if (nt != NULL && nt->expr.base == CF_TYPE_CHOICE) {
            unsigned int m;
            char acname[CF_MAX_IDENT_LEN];
            opt_c_name(acname, expr->alias_name, sizeof(acname));
            for (m = 0U; m < nt->expr.nmembers; m++) {
                if (strcmp(nt->expr.members[m], opt->default_val) == 0) {
                    char eval[CF_MAX_IDENT_LEN * 3];
                    enum_val(g, acname, opt->default_val, eval, sizeof(eval));
                    fprintf(g->fc, "\tout->%s = %s;\n", e->struct_path, eval);
                    return;
                }
            }
        }
        /* fallback: numeric */
        fprintf(g->fc, "\tout->%s = %s;\n", e->struct_path, opt->default_val);
    } else if (expr->base == CF_TYPE_CHOICE) {
        /* emit enum constant when resolved from a named alias */
        if (opt->type.base == CF_TYPE_ALIAS) {
            const cf_named_type_t *nt2 = find_named_type(g->file, opt->type.alias_name);
            if (nt2 != NULL) {
                unsigned int m2;
                char acname2[CF_MAX_IDENT_LEN];
                opt_c_name(acname2, opt->type.alias_name, sizeof(acname2));
                for (m2 = 0U; m2 < nt2->expr.nmembers; m2++) {
                    if (strcmp(nt2->expr.members[m2], opt->default_val) == 0) {
                        char eval2[CF_MAX_IDENT_LEN * 3];
                        enum_val(g, acname2, opt->default_val, eval2, sizeof(eval2));
                        fprintf(g->fc, "\tout->%s = %s;\n", e->struct_path, eval2);
                        return;
                    }
                }
            }
        }
        fprintf(g->fc, "\tout->%s = 0; /* default: %s */\n",
                e->struct_path, opt->default_val);
    } else if (expr->base == CF_TYPE_DURATION || expr->base == CF_TYPE_BYTES ||
               expr->base == CF_TYPE_FREQUENCY || expr->base == CF_TYPE_RATIO) {
        const cf_qgroup_t *qg = qgroup_for(expr->base);
        if (g->file->schema_version >= 2U && qg != NULL &&
            opt->default_val[0] != '\0') {
            /* generate-time check: the default's unit must be allowed */
            if (expr->nunits > 0U) {
                const char *du = opt->default_val;
                unsigned int duok = 0U, uk;
                while (*du && ((*du >= '0' && *du <= '9') ||
                               *du == '.' || *du == '_')) du++;
                for (uk = 0U; uk < expr->nunits; uk++) {
                    if (strcmp(du, expr->units[uk]) == 0) { duok = 1U; break; }
                }
                if (!duok) {
                    fprintf(stderr, "cliforge: option '%s': default '%s' "
                            "uses a unit not in its units list\n",
                            opt->name, opt->default_val);
                    g->had_error = 1;
                }
            }
            /* parse the default string into the typed { value, unit } */
            fprintf(g->fc, "\t(void)cf__parse_%s(\"%s\", &out->%s);\n",
                    qg->gname, opt->default_val, e->struct_path);
        } else {
            /* v1 (or ratio): strip unit suffix, emit the numeric part */
            const char *p2 = opt->default_val;
            char numpart[CF_MAX_IDENT_LEN];
            unsigned int ni = 0U;
            while (*p2 && ((*p2 >= '0' && *p2 <= '9') || *p2 == '_' || *p2 == '.')) {
                if (*p2 != '_') numpart[ni++] = *p2;
                p2++;
            }
            numpart[ni] = '\0';
            if (ni > 0U) {
                fprintf(g->fc, "\tout->%s = %s; /* %s */\n",
                        e->struct_path, numpart, opt->default_val);
            } else {
                fprintf(g->fc, "\tout->%s = 0; /* %s */\n",
                        e->struct_path, opt->default_val);
            }
        }
    } else if (expr->base == CF_TYPE_FLOAT || expr->base == CF_TYPE_DOUBLE) {
        /* Float/double defaults are valid C literals (0.5, 1e-9, -1.0, etc.)
         * including scientific notation — emit verbatim, no stripping needed. */
        if (opt->default_val[0] != '\0') {
            fprintf(g->fc, "\tout->%s = %s;\n", e->struct_path, opt->default_val);
        } else {
            fprintf(g->fc, "\tout->%s = 0;\n", e->struct_path);
        }
    } else if (expr->base == CF_TYPE_SINT8  || expr->base == CF_TYPE_SINT16 ||
               expr->base == CF_TYPE_SINT32 || expr->base == CF_TYPE_SINT64) {
        /* Signed integer defaults may start with '-'; emit verbatim. */
        if (opt->default_val[0] != '\0') {
            fprintf(g->fc, "\tout->%s = %s;\n", e->struct_path, opt->default_val);
        } else {
            fprintf(g->fc, "\tout->%s = 0;\n", e->struct_path);
        }
    } else {
        /* Unsigned integers and other types: strip any trailing unit suffix
         * (e.g. a bare numeric that slipped through without a unit match). */
        const char *p2 = opt->default_val;
        char numpart[CF_MAX_IDENT_LEN];
        unsigned int ni = 0U;
        if (*p2 >= '0' && *p2 <= '9') {
            while (*p2 && ((*p2 >= '0' && *p2 <= '9') || *p2 == '_' || *p2 == '.')) {
                if (*p2 != '_') numpart[ni++] = *p2;
                p2++;
            }
            numpart[ni] = '\0';
            fprintf(g->fc, "\tout->%s = %s; /* %s */\n",
                    e->struct_path, numpart, opt->default_val);
        } else if (opt->default_val[0] != '\0') {
            fprintf(g->fc, "\tout->%s = %s;\n", e->struct_path, opt->default_val);
        } else {
            fprintf(g->fc, "\tout->%s = 0;\n", e->struct_path);
        }
    }
}

/* Emit a v2 typed-compound field parser.  `rec` is the record accessor
 * (e.g. "out->main." or "rec->").  String/path/ratio fields are copied
 * directly; numeric, bool, float, quantity and choice fields are read into
 * a temp string and then converted into their typed storage. */
static void emit_typed_compound_parse(gen_ctx_t *g, const cf_named_type_t *nt,
                                      const char *rec)
{
    unsigned int fi;
    unsigned int nf = nt->expr.nfields;

#define CF_FLD_STRLIKE(b) ((b) == CF_TYPE_STRING || (b) == CF_TYPE_PATH || \
                           (b) == CF_TYPE_FILE   || (b) == CF_TYPE_DIR  || \
                           (b) == CF_TYPE_RATIO)

    fprintf(g->fc, "\t\t\t{\n");
    /* declarations first (C89) */
    for (fi = 0U; fi < nf; fi++) {
        const cf_field_t *fld = &nt->expr.fields[fi];
        char fcn[CF_MAX_IDENT_LEN];
        opt_c_name(fcn, fld->name, sizeof(fcn));
        if (!CF_FLD_STRLIKE(fld->base))
            fprintf(g->fc, "\t\t\tchar cf__t_%s[64];\n", fcn);
    }
    fprintf(g->fc, "\t\t\tcf__kv_t cf__f_[%u];\n", nf);
    /* init temps */
    for (fi = 0U; fi < nf; fi++) {
        const cf_field_t *fld = &nt->expr.fields[fi];
        char fcn[CF_MAX_IDENT_LEN];
        opt_c_name(fcn, fld->name, sizeof(fcn));
        if (!CF_FLD_STRLIKE(fld->base))
            fprintf(g->fc, "\t\t\tcf__t_%s[0] = '\\0';\n", fcn);
    }
    /* kv setup */
    for (fi = 0U; fi < nf; fi++) {
        const cf_field_t *fld = &nt->expr.fields[fi];
        char fcn[CF_MAX_IDENT_LEN];
        unsigned int slen;
        opt_c_name(fcn, fld->name, sizeof(fcn));
        slen = (fld->str_len > 0U) ? fld->str_len : 64U;
        if (CF_FLD_STRLIKE(fld->base)) {
            fprintf(g->fc,
                "\t\t\tcf__f_[%u].key = \"%s\"; cf__f_[%u].dst = (char*)&%s%s; "
                "cf__f_[%u].dstsz = %uU;\n",
                fi, fld->name, fi, rec, fcn, fi, slen);
        } else {
            fprintf(g->fc,
                "\t\t\tcf__f_[%u].key = \"%s\"; cf__f_[%u].dst = cf__t_%s; "
                "cf__f_[%u].dstsz = 64U;\n",
                fi, fld->name, fi, fcn, fi);
        }
    }
    fprintf(g->fc, "\t\t\tcf__parse_compound(val, cf__f_, %u);\n", nf);
    /* conversions */
    for (fi = 0U; fi < nf; fi++) {
        const cf_field_t *fld = &nt->expr.fields[fi];
        char fcn[CF_MAX_IDENT_LEN];
        opt_c_name(fcn, fld->name, sizeof(fcn));
        if (CF_FLD_STRLIKE(fld->base)) continue;

        if (qgroup_for(fld->base) != NULL) {
            const cf_qgroup_t *fqg = qgroup_for(fld->base);
            fprintf(g->fc,
                "\t\t\tif (cf__t_%s[0] != '\\0') (void)cf__parse_%s(cf__t_%s, &%s%s);\n",
                fcn, fqg->gname, fcn, rec, fcn);
        } else if (fld->base == CF_TYPE_ALIAS) {
            const cf_named_type_t *an = find_named_type(g->file, fld->alias_name);
            if (an != NULL && an->expr.base == CF_TYPE_CHOICE) {
                char acn[CF_MAX_IDENT_LEN];
                unsigned int m;
                opt_c_name(acn, fld->alias_name, sizeof(acn));
                fprintf(g->fc, "\t\t\tif (cf__t_%s[0] != '\\0') {\n", fcn);
                for (m = 0U; m < an->expr.nmembers; m++) {
                    char eval[CF_MAX_IDENT_LEN * 3];
                    enum_val(g, acn, an->expr.members[m], eval, sizeof(eval));
                    fprintf(g->fc,
                        "\t\t\t\t%sif (strcmp(cf__t_%s, \"%s\") == 0) %s%s = %s;\n",
                        (m == 0U) ? "" : "else ",
                        fcn, an->expr.members[m], rec, fcn, eval);
                }
                fprintf(g->fc, "\t\t\t}\n");
            }
            /* alias->compound: nested compound unsupported, value discarded */
        } else if (fld->base == CF_TYPE_BOOL || fld->base == CF_TYPE_FLAG) {
            fprintf(g->fc,
                "\t\t\tif (cf__t_%s[0] != '\\0') %s%s = (strcmp(cf__t_%s, \"true\") == 0 "
                "|| strcmp(cf__t_%s, \"1\") == 0) ? 1 : 0;\n",
                fcn, rec, fcn, fcn, fcn);
        } else if (fld->base == CF_TYPE_FLOAT) {
            fprintf(g->fc,
                "\t\t\tif (cf__t_%s[0] != '\\0') %s%s = (float)strtod(cf__t_%s, NULL);\n",
                fcn, rec, fcn, fcn);
        } else if (fld->base == CF_TYPE_DOUBLE) {
            fprintf(g->fc,
                "\t\t\tif (cf__t_%s[0] != '\\0') %s%s = strtod(cf__t_%s, NULL);\n",
                fcn, rec, fcn, fcn);
        } else if (fld->base >= CF_TYPE_UINT8 && fld->base <= CF_TYPE_UINT64) {
            fprintf(g->fc,
                "\t\t\tif (cf__t_%s[0] != '\\0') %s%s = (%s)strtoul(cf__t_%s, NULL, 0);\n",
                fcn, rec, fcn, c_type_str(fld->base), fcn);
        } else {
            fprintf(g->fc,
                "\t\t\tif (cf__t_%s[0] != '\\0') %s%s = (%s)strtol(cf__t_%s, NULL, 0);\n",
                fcn, rec, fcn, c_type_str(fld->base), fcn);
        }
    }
    fprintf(g->fc, "\t\t\t}\n");
#undef CF_FLD_STRLIKE
}

/* Emit the type-specific value parser for one option */
static void emit_value_parser(gen_ctx_t *g, const flat_opt_t *e)
{
    const cf_option_t    *opt  = e->opt;
    const cf_type_expr_t *expr = resolve_alias(g->file, &opt->type);

    switch (expr->base) {
    case CF_TYPE_FLAG:
        fprintf(g->fc, "\t\t\tout->%s = 1;\n", e->struct_path);
        fprintf(g->fc, "\t\t\tcontinue;\n");
        return;
    case CF_TYPE_BOOL:
        fprintf(g->fc,
                "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                "\t\t\tout->%s = (strcmp(val,\"true\")==0||strcmp(val,\"yes\")==0||\n"
                "\t\t\t           strcmp(val,\"on\")==0||strcmp(val,\"1\")==0) ? 1 : 0;\n"
                "\t\t\tcontinue;\n",
                e->long_name, e->struct_path);
        return;
    case CF_TYPE_STRING:
    case CF_TYPE_PATH:
    case CF_TYPE_FILE:
    case CF_TYPE_DIR: {
        unsigned int slen = (expr->str_len > 0U) ? expr->str_len : 256U;
        if (opt->multiple.enabled && opt->multiple.max > 1U) {
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\tif (out->%s_count < %u) {\n"
                    "\t\t\t\t(void)strncpy(out->%s[out->%s_count++], val, %uU - 1U);\n"
                    "\t\t\t}\n"
                    "\t\t\tcontinue;\n",
                    e->long_name,
                    e->struct_path, opt->multiple.max,
                    e->struct_path, e->struct_path, slen);
        } else {
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\t(void)strncpy(out->%s, val, sizeof(out->%s) - 1U);\n"
                    "\t\t\tcontinue;\n",
                    e->long_name, e->struct_path, e->struct_path);
        }
        return;
    }
    case CF_TYPE_ALIAS: {
        const cf_named_type_t *nt = find_named_type(g->file, expr->alias_name);
        if (nt != NULL && nt->expr.base == CF_TYPE_CHOICE) {
            char acname[CF_MAX_IDENT_LEN];
            unsigned int m;
            opt_c_name(acname, expr->alias_name, sizeof(acname));
            if (opt->multiple.enabled && opt->multiple.max > 1U) {
                /* compound multiple — not yet implemented */
                fprintf(g->fc,
                        "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                        "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                        "\t\t\t/* TODO: parse compound multiple */\n"
                        "\t\t\tcontinue;\n",
                        e->long_name);
            } else {
                /* choice parse: compare against each member */
                fprintf(g->fc,
                        "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                        "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n",
                        e->long_name);
                for (m = 0U; m < nt->expr.nmembers; m++) {
                    char eval[CF_MAX_IDENT_LEN * 3];
                    enum_val(g, acname, nt->expr.members[m], eval, sizeof(eval));
                    fprintf(g->fc,
                            "\t\t\t%sif (strcmp(val, \"%s\") == 0) out->%s = %s;\n",
                            (m == 0U) ? "" : "else ",
                            nt->expr.members[m], e->struct_path, eval);
                }
                emit_choice_else(g, opt, e->long_name,
                                 nt->expr.members, nt->expr.nmembers);
            }
        } else if (nt != NULL && nt->expr.base == CF_TYPE_COMPOUND) {
            /* Compound type: parse key=value,key=value */
            char acname[CF_MAX_IDENT_LEN];
            unsigned int fi;
            opt_c_name(acname, expr->alias_name, sizeof(acname));
            if (g->file->schema_version >= 2U) {
                if (opt->multiple.enabled && opt->multiple.max > 1U) {
                    fprintf(g->fc,
                        "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                        "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                        "\t\t\tif (out->%s_count < %u) {\n"
                        "\t\t\tstruct %s_%s *rec = &out->%s[out->%s_count++];\n",
                        e->long_name, e->struct_path, opt->multiple.max,
                        g->prefix, acname, e->struct_path, e->struct_path);
                    emit_typed_compound_parse(g, nt, "rec->");
                    fprintf(g->fc, "\t\t\t}\n\t\t\tcontinue;\n");
                } else {
                    char racc[CF_MAX_IDENT_LEN * 2 + 8];
                    fprintf(g->fc,
                        "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                        "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n",
                        e->long_name);
                    (void)snprintf(racc, sizeof(racc), "out->%s.", e->struct_path);
                    emit_typed_compound_parse(g, nt, racc);
                    fprintf(g->fc, "\t\t\tcontinue;\n");
                }
            } else {
            if (opt->multiple.enabled && opt->multiple.max > 1U) {
                unsigned int maxn = opt->multiple.max;
                fprintf(g->fc,
                        "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                        "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                        "\t\t\tif (out->%s_count < %u) {\n"
                        "\t\t\t\tstruct %s_%s *rec = &out->%s[out->%s_count++];\n"
                        "\t\t\t\t{ cf__kv_t cf__f_[%u];\n",
                        e->long_name,
                        e->struct_path, maxn,
                        g->prefix, acname,
                        e->struct_path, e->struct_path,
                        (unsigned int)nt->expr.nfields);
                for (fi = 0U; fi < nt->expr.nfields; fi++) {
                    const cf_field_t *fld = &nt->expr.fields[fi];
                    char fcname[CF_MAX_IDENT_LEN];
                    unsigned int slen;
                    opt_c_name(fcname, fld->name, sizeof(fcname));
                    slen = (fld->str_len > 0U) ? fld->str_len : 256U;
                    fprintf(g->fc,
                            "\t\t\t\tcf__f_[%u].key = \"%s\"; cf__f_[%u].dst = (char*)rec->%s; cf__f_[%u].dstsz = %uU;\n",
                            fi, fld->name, fi, fcname, fi, slen);
                }
                fprintf(g->fc, "\t\t\t\tcf__parse_compound(val, cf__f_, %u); }\n\t\t\t}\n\t\t\tcontinue;\n",
                        (unsigned int)nt->expr.nfields);
            } else {
                fprintf(g->fc,
                        "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                        "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                        "\t\t\t{ cf__kv_t cf__f_[%u];\n",
                        e->long_name, (unsigned int)nt->expr.nfields);
                for (fi = 0U; fi < nt->expr.nfields; fi++) {
                    const cf_field_t *fld = &nt->expr.fields[fi];
                    char fcname[CF_MAX_IDENT_LEN];
                    unsigned int slen;
                    opt_c_name(fcname, fld->name, sizeof(fcname));
                    slen = (fld->str_len > 0U) ? fld->str_len : 256U;
                    fprintf(g->fc,
                            "\t\t\tcf__f_[%u].key = \"%s\"; cf__f_[%u].dst = (char*)&out->%s.%s; cf__f_[%u].dstsz = %uU;\n",
                            fi, fld->name, fi, e->struct_path, fcname, fi, slen);
                }
                fprintf(g->fc, "\t\t\tcf__parse_compound(val, cf__f_, %u); }\n\t\t\tcontinue;\n",
                        (unsigned int)nt->expr.nfields);
            }
            }
                } else {
            /* unresolved alias — treat as string */
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\t(void)strncpy(out->%s, val, sizeof(out->%s) - 1U);\n"
                    "\t\t\tcontinue;\n",
                    e->long_name, e->struct_path, e->struct_path);
        }
        return;
    }
    case CF_TYPE_SINT8:  case CF_TYPE_SINT16:
    case CF_TYPE_SINT32: case CF_TYPE_SINT64:
        if (e->opt->multiple.enabled && e->opt->multiple.max > 1U) {
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\tif (out->%s_count < %u) out->%s[out->%s_count++] = (%s)strtol(val, NULL, 0);\n"
                    "\t\t\tcontinue;\n",
                    e->long_name, e->struct_path, e->opt->multiple.max,
                    e->struct_path, e->struct_path, c_type_str(expr->base));
        } else {
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\tout->%s = (%s)strtol(val, NULL, 0);\n",
                    e->long_name, e->struct_path, c_type_str(expr->base));
            emit_range_check(g, e->opt, e->long_name, e->struct_path, expr);
            fprintf(g->fc, "\t\t\tcontinue;\n");
        }
        return;
    case CF_TYPE_UINT8:  case CF_TYPE_UINT16:
    case CF_TYPE_UINT32: case CF_TYPE_UINT64:
        if (e->opt->multiple.enabled && e->opt->multiple.max > 1U) {
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\tif (out->%s_count < %u) out->%s[out->%s_count++] = (%s)strtoul(val, NULL, 0);\n"
                    "\t\t\tcontinue;\n",
                    e->long_name, e->struct_path, e->opt->multiple.max,
                    e->struct_path, e->struct_path, c_type_str(expr->base));
        } else {
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\tout->%s = (%s)strtoul(val, NULL, 0);\n",
                    e->long_name, e->struct_path, c_type_str(expr->base));
            emit_range_check(g, e->opt, e->long_name, e->struct_path, expr);
            fprintf(g->fc, "\t\t\tcontinue;\n");
        }
        return;
    case CF_TYPE_FLOAT:
        fprintf(g->fc,
                "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                "\t\t\tout->%s = (float)strtod(val, NULL);\n"
                "\t\t\tcontinue;\n",
                e->long_name, e->struct_path);
        return;
    case CF_TYPE_DOUBLE:
        fprintf(g->fc,
                "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                "\t\t\tout->%s = strtod(val, NULL);\n"
                "\t\t\tcontinue;\n",
                e->long_name, e->struct_path);
        return;
    case CF_TYPE_DURATION:
    case CF_TYPE_BYTES:
    case CF_TYPE_FREQUENCY:
    case CF_TYPE_RATIO:
        {
            const cf_qgroup_t *qg = qgroup_for(expr->base);
            fprintf(g->fc,
                "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n",
                e->long_name);
            if (g->file->schema_version >= 2U && qg != NULL) {
                char unitcond[CF_MAX_MEMBERS * 256];
                char gup[CF_MAX_IDENT_LEN];
                unsigned int uu;
                str_upper(gup, qg->gname, sizeof(gup));
                unitcond[0] = '\0';
                for (uu = 0U; uu < expr->nunits; uu++) {
                    char uup[16];
                    char term[256];
                    str_upper(uup, expr->units[uu], sizeof(uup));
                    (void)snprintf(term, sizeof(term),
                                   "%scf__q.unit == %s_%s_%s",
                                   (uu == 0U) ? "" : " || ",
                                   g->PREFIX, gup, uup);
                    (void)strncat(unitcond, term,
                                  sizeof(unitcond) - strlen(unitcond) - 1U);
                }
                if (expr->nunits > 0U) {
                    fprintf(g->fc,
                        "\t\t\t{ struct %s_%s cf__q;\n"
                        "\t\t\tif (cf__parse_%s(val, &cf__q) == 0 && (%s)) "
                        "out->%s = cf__q;\n",
                        g->prefix, qg->gname, qg->gname, unitcond,
                        e->struct_path);
                } else {
                    fprintf(g->fc,
                        "\t\t\t{ struct %s_%s cf__q;\n"
                        "\t\t\tif (cf__parse_%s(val, &cf__q) == 0) "
                        "out->%s = cf__q;\n",
                        g->prefix, qg->gname, qg->gname, e->struct_path);
                }
                if (on_error_warn(g, e->opt)) {
                    fprintf(g->fc,
                        "\t\t\telse { fprintf(stderr, \"warning: --%s: bad value '%%s' "
                        "(keeping default)\\n\", val); } }\n",
                        e->long_name);
                } else {
                    fprintf(g->fc,
                        "\t\t\telse { fprintf(stderr, \"error: --%s: bad value '%%s'\\n\", "
                        "val); return -1; } }\n",
                        e->long_name);
                }
                fprintf(g->fc, "\t\t\tcontinue;\n");
            } else {
                fprintf(g->fc,
                    "\t\t\tout->%s = (uint64_t)strtoul(val, NULL, 0);\n"
                    "\t\t\tcontinue;\n",
                    e->struct_path);
            }
        }
        return;
    case CF_TYPE_CHOICE:
        /* inline/resolved choice -- if original was alias, emit enum comparisons */
        if (e->opt->type.base == CF_TYPE_ALIAS) {
            char acname3[CF_MAX_IDENT_LEN];
            unsigned int m3;
            opt_c_name(acname3, e->opt->type.alias_name, sizeof(acname3));
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n",
                    e->long_name);
            for (m3 = 0U; m3 < expr->nmembers; m3++) {
                char eval3[CF_MAX_IDENT_LEN * 3];
                enum_val(g, acname3, expr->members[m3], eval3, sizeof(eval3));
                fprintf(g->fc,
                        "\t\t\t%sif (strcmp(val, \"%s\") == 0) out->%s = %s;\n",
                        (m3 == 0U) ? "" : "else ",
                        expr->members[m3], e->struct_path, eval3);
            }
            emit_choice_else(g, e->opt, e->long_name,
                             expr->members, expr->nmembers);
        } else {
            /* inline choice: accept as integer */
            fprintf(g->fc,
                    "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                    "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                    "\t\t\tout->%s = (int)strtol(val, NULL, 0);\n"
                    "\t\t\tcontinue;\n",
                    e->long_name, e->struct_path);
        }
        return;
    case CF_TYPE_COMPOUND: {
        /* resolve_alias already resolved the alias; original alias name is in opt->type */
        if (opt->type.base == CF_TYPE_ALIAS) {
            char acname4[CF_MAX_IDENT_LEN];
            const cf_named_type_t *nt4;
            unsigned int fi4;
            opt_c_name(acname4, opt->type.alias_name, sizeof(acname4));
            nt4 = find_named_type(g->file, opt->type.alias_name);
            if (nt4 != NULL) {
                if (g->file->schema_version >= 2U) {
                    if (opt->multiple.enabled && opt->multiple.max > 1U) {
                        fprintf(g->fc,
                            "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                            "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                            "\t\t\tif (out->%s_count < %u) {\n"
                            "\t\t\tstruct %s_%s *rec = &out->%s[out->%s_count++];\n",
                            e->long_name, e->struct_path, opt->multiple.max,
                            g->prefix, acname4, e->struct_path, e->struct_path);
                        emit_typed_compound_parse(g, nt4, "rec->");
                        fprintf(g->fc, "\t\t\t}\n\t\t\tcontinue;\n");
                    } else {
                        char racc4[CF_MAX_IDENT_LEN * 2 + 8];
                        fprintf(g->fc,
                            "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                            "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n",
                            e->long_name);
                        (void)snprintf(racc4, sizeof(racc4), "out->%s.", e->struct_path);
                        emit_typed_compound_parse(g, nt4, racc4);
                        fprintf(g->fc, "\t\t\tcontinue;\n");
                    }
                } else {
                if (opt->multiple.enabled && opt->multiple.max > 1U) {
                    unsigned int maxn4 = opt->multiple.max;
                    fprintf(g->fc,
                            "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                            "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                            "\t\t\tif (out->%s_count < %u) {\n"
                            "\t\t\t\tstruct %s_%s *rec = &out->%s[out->%s_count++];\n"
                            "\t\t\t\t{ cf__kv_t cf__f_[%u];\n",
                            e->long_name,
                            e->struct_path, maxn4,
                            g->prefix, acname4,
                            e->struct_path, e->struct_path,
                            (unsigned int)nt4->expr.nfields);
                    for (fi4 = 0U; fi4 < nt4->expr.nfields; fi4++) {
                        const cf_field_t *fld4 = &nt4->expr.fields[fi4];
                        char fcname4[CF_MAX_IDENT_LEN];
                        unsigned int slen4;
                        opt_c_name(fcname4, fld4->name, sizeof(fcname4));
                        slen4 = (fld4->str_len > 0U) ? fld4->str_len : 256U;
                        fprintf(g->fc,
                                "\t\t\t\tcf__f_[%u].key = \"%s\"; cf__f_[%u].dst = (char*)rec->%s; cf__f_[%u].dstsz = %uU;\n",
                                fi4, fld4->name, fi4, fcname4, fi4, slen4);
                    }
                    fprintf(g->fc, "\t\t\t\tcf__parse_compound(val, cf__f_, %u); }\n\t\t\t}\n\t\t\tcontinue;\n",
                            (unsigned int)nt4->expr.nfields);
                } else {
                    fprintf(g->fc,
                            "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                            "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                            "\t\t\t{ cf__kv_t cf__f_[%u];\n",
                            e->long_name, (unsigned int)nt4->expr.nfields);
                    for (fi4 = 0U; fi4 < nt4->expr.nfields; fi4++) {
                        const cf_field_t *fld4 = &nt4->expr.fields[fi4];
                        char fcname4[CF_MAX_IDENT_LEN];
                        unsigned int slen4;
                        opt_c_name(fcname4, fld4->name, sizeof(fcname4));
                        slen4 = (fld4->str_len > 0U) ? fld4->str_len : 256U;
                        fprintf(g->fc,
                                "\t\t\tcf__f_[%u].key = \"%s\"; cf__f_[%u].dst = (char*)&out->%s.%s; cf__f_[%u].dstsz = %uU;\n",
                                fi4, fld4->name, fi4, e->struct_path, fcname4, fi4, slen4);
                    }
                    fprintf(g->fc, "\t\t\tcf__parse_compound(val, cf__f_, %u); }\n\t\t\tcontinue;\n",
                            (unsigned int)nt4->expr.nfields);
                }
                }
                return;
            }
        }
        /* inline compound or unresolved — treat as opaque string */
        fprintf(g->fc,
                "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                "\t\t\tif (!val) { fprintf(stderr, \"error: --%s requires argument\\n\"); return -1; }\n"
                "\t\t\t(void)val; /* compound: not parseable inline */\n"
                "\t\t\tcontinue;\n",
                e->long_name);
        return;
    }
    default:
        fprintf(g->fc,
                "\t\t\tval = cf__next_arg(&i, argc, argv);\n"
                "\t\t\t(void)val;\n"
                "\t\t\tcontinue;\n");
        return;
    }
}

/* Return a short all-caps type label for Style-D help output.
 * Resolved after alias expansion (call resolve_alias first). */
static const char *type_hint_str(const cf_type_expr_t *expr)
{
    switch (expr->base) {
    case CF_TYPE_FLAG:      return "";
    case CF_TYPE_BOOL:      return "BOOL";
    case CF_TYPE_SINT8:     return "INT8";
    case CF_TYPE_SINT16:    return "INT16";
    case CF_TYPE_SINT32:    return "INT32";
    case CF_TYPE_SINT64:    return "INT64";
    case CF_TYPE_UINT8:     return "UINT8";
    case CF_TYPE_UINT16:    return "UINT16";
    case CF_TYPE_UINT32:    return "UINT32";
    case CF_TYPE_UINT64:    return "UINT64";
    case CF_TYPE_FLOAT:     return "FLOAT";
    case CF_TYPE_DOUBLE:    return "DOUBLE";
    case CF_TYPE_STRING:    return "STRING";
    case CF_TYPE_PATH:      return "PATH";
    case CF_TYPE_FILE:      return "FILE";
    case CF_TYPE_DIR:       return "DIR";
    case CF_TYPE_DURATION:  return "TIME";
    case CF_TYPE_BYTES:     return "BYTES";
    case CF_TYPE_FREQUENCY: return "FREQ";
    case CF_TYPE_RATIO:     return "RATIO";
    case CF_TYPE_CHOICE:    return "ENUM";
    case CF_TYPE_COMPOUND:  return "KEY=VAL";
    default:                return "VALUE";
    }
}

/* Escape a string for safe embedding in a C string literal.
 * Converts: " -> \" and \ -> \\ */
static unsigned int c_str_escape(char *dst, unsigned int dstsz, const char *src)
{
    unsigned int i = 0U, j = 0U;
    while (src[i] != '\0' && j + 2U < dstsz) {
        if (src[i] == '"' || src[i] == '\\') dst[j++] = '\\';
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
    return j;
}

/* Build the "(default: VALUE)" suffix for a help line.
 * Returns an empty string when there is nothing useful to show.
 *
 * %.*s with an explicit precision is used for the string and numeric
 * branches so that GCC can statically verify the output fits in buf
 * (-Wformat-truncation).  The precision is computed from bufsz minus the
 * fixed overhead of each format literal:
 *   "  [default: \"...\"]"  overhead = 16  (13 prefix + 2 suffix + NUL)
 *   "  [default: ...]"      overhead = 14  (12 prefix + 1 suffix + NUL)
 * Truncation of long default values in --help output is intentional. */
static void fmt_help_default(char *buf, unsigned int bufsz,
                              const cf_option_t *opt,
                              const cf_type_expr_t *expr)
{
    const char *val = opt->default_val;
    if (val[0] == '\0') { buf[0] = '\0'; return; }

    if (expr->base == CF_TYPE_FLAG || expr->base == CF_TYPE_BOOL) {
        /* "on" / "off" are at most 3 bytes — no truncation possible. */
        const char *v = (val[0] == '0' || strcmp(val, "false") == 0)
                        ? "off" : "on";
        (void)snprintf(buf, (size_t)bufsz, "  [default: %s]", v);
    } else if (expr->base == CF_TYPE_STRING || expr->base == CF_TYPE_PATH ||
               expr->base == CF_TYPE_FILE   || expr->base == CF_TYPE_DIR) {
        /* Overhead of '  [default: ""]' = 16 bytes (incl. NUL). */
        static const unsigned int STR_OVERHEAD = 16U;
        int maxcontent;
        char esc[256];
        if (val[0] == '\0') { buf[0] = '\0'; return; }
        c_str_escape(esc, sizeof(esc), val);
        maxcontent = (bufsz > STR_OVERHEAD) ? (int)(bufsz - STR_OVERHEAD) : 0;
        (void)snprintf(buf, (size_t)bufsz, "  [default: \"%.*s\"]",
                       maxcontent, esc);
    } else {
        /* Overhead of '  [default: ]' = 14 bytes (incl. NUL). */
        static const unsigned int NUM_OVERHEAD = 14U;
        int maxcontent;
        maxcontent = (bufsz > NUM_OVERHEAD) ? (int)(bufsz - NUM_OVERHEAD) : 0;
        (void)snprintf(buf, (size_t)bufsz, "  [default: %.*s]",
                       maxcontent, val);
    }
}

/* Build a " (units: us|ms|s)" hint for a quantity option's help line. */
static void fmt_units_hint(char *buf, unsigned int sz, const cf_type_expr_t *e)
{
    unsigned int u;
    buf[0] = '\0';
    if (e->nunits == 0U) return;
    cf_strlcpy(buf, " (units: ", sz);
    for (u = 0U; u < e->nunits; u++) {
        if (u > 0U) (void)strncat(buf, "|", sz - strlen(buf) - 1U);
        (void)strncat(buf, e->units[u], sz - strlen(buf) - 1U);
    }
    (void)strncat(buf, ")", sz - strlen(buf) - 1U);
}

static void gen_source(gen_ctx_t *g)
{
    flat_list_t  fl;
    unsigned int i;
    unsigned int si;

    memset(&fl, 0, sizeof(fl));
    collect_options(g, &fl);

    /* --- check if any option uses a compound type --- */
    {
        unsigned int ci;
        for (ci = 0U; ci < fl.n; ci++) {
            const cf_type_expr_t *cexpr = resolve_alias(g->file, &fl.entries[ci].opt->type);
            if (cexpr->base == CF_TYPE_COMPOUND) {
                g->has_compound = 1;
                break;
            }
        }
    }

    /* --- preamble --- */
    fprintf(g->fc,
            "/* Generated by cliforge — DO NOT EDIT */\n"
            "#include \"%s.h\"\n"
            "#include <stdio.h>\n"
            "#include <stdlib.h>\n"
            "#include <string.h>\n\n",
            g->output);

    /* --- internal runtime helpers --- */
    fprintf(g->fc,
        "/* ------- internal helpers ------- */\n"
        "typedef struct { const char *key; char *dst; unsigned int dstsz; } cf__kv_t;\n\n"
        "static const char *cf__next_arg(int *i, int argc, char *const *argv)\n"
        "{\n"
        "\tconst char *arg = argv[*i];\n"
        "\tconst char *eq  = strchr(arg, '=');\n"
        "\tif (eq != NULL) return eq + 1;\n"
        "\tif (*i + 1 < argc && argv[*i + 1][0] != '-') { (*i)++; return argv[*i]; }\n"
        "\treturn NULL;\n"
        "}\n\n");

    gen_quantity_defs(g);

    if (g->has_compound) {
        fprintf(g->fc,
            "static void cf__parse_compound(const char *s, cf__kv_t *fields, unsigned int nfields)\n"
            "{\n"
            "\tchar buf[4096];\n"
            "\tchar *tok;\n"
            "\tunsigned int fi;\n"
            "\t(void)strncpy(buf, s, sizeof(buf) - 1U);\n"
            "\ttok = strtok(buf, \",\");\n"
            "\twhile (tok != NULL) {\n"
            "\t\tchar *eq = strchr(tok, '=');\n"
            "\t\tif (eq != NULL) {\n"
            "\t\t\t*eq = '\\0';\n"
            "\t\t\tfor (fi = 0U; fi < nfields; fi++) {\n"
            "\t\t\t\tif (strcmp(tok, fields[fi].key) == 0) {\n"
            "\t\t\t\t\t(void)strncpy(fields[fi].dst, eq + 1, fields[fi].dstsz - 1U);\n"
            "\t\t\t\t\tbreak;\n"
            "\t\t\t\t}\n"
            "\t\t\t}\n"
            "\t\t}\n"
            "\t\ttok = strtok(NULL, \",\");\n"
            "\t}\n"
            "}\n\n");
    }

    /* Help lines are emitted directly in cc_cmdline_help() as fputs calls
     * to stay within the C89 509-character string literal length limit. */

    /* --- version string --- */
    fprintf(g->fc,
            "static const char %s__version[] = \"%s %s\";\n\n",
            g->prefix, g->app,
            (g->file->meta.version[0] != '\0') ? g->file->meta.version : "0.0.0");

    /* --- parse function --- */
    fprintf(g->fc,
            "int %s_cmdline_parse(int argc, char *const *argv,\n"
            "                     struct %s_cmdline *out)\n"
            "{\n"
            "\tint i;\n"
            "\tconst char *arg;\n"
            "\tconst char *val;\n\n"
            "\t(void)memset(out, 0, sizeof(*out));\n",
            g->prefix, g->prefix);

    /* emit defaults for top-level options */
    for (i = 0U; i < (unsigned int)fl.n; i++) {
        emit_default(g, &fl.entries[i]);
    }
    fprintf(g->fc, "\n");

    /* subcommand parsing */
    if (g->file->nsubcommands > 0U) {
        fprintf(g->fc,
                "\t/* --- subcommand detection --- */\n"
                "\tfor (i = 1; i < argc; i++) {\n"
                "\t\targ = argv[i];\n"
                "\t\tif (arg[0] == '-') continue; /* skip option arguments */\n");
        for (i = 0U; i < g->file->nsubcommands; i++) {
            const cf_subcommand_t *sub = &g->file->subcommands[i];
            char scname[CF_MAX_IDENT_LEN];
            str_upper(scname, sub->name, sizeof(scname));
            fprintf(g->fc,
                    "\t\t%sif (strcmp(arg, \"%s\") == 0) { out->subcmd = %s_CMD_%s; break; }\n",
                    (i == 0U) ? "" : "else ", sub->name, g->PREFIX, scname);
        }
        fprintf(g->fc, "\t}\n\n");
    }

    fprintf(g->fc,
            "\t/* --- main option parse loop --- */\n"
            "\tfor (i = 1; i < argc; i++) {\n"
            "\t\targ = argv[i];\n"
            "\t\tval = NULL;\n\n"
            "\t\t/* skip subcommand token */\n"
            "\t\tif (arg[0] != '-') continue;\n\n"
            "\t\t/* --help */\n"
            "\t\tif (strcmp(arg, \"--help\") == 0 || strcmp(arg, \"-h\") == 0) {\n"
            "\t\t\t%s_cmdline_help();\n"
            "\t\t\treturn 1;\n"
            "\t\t}\n"
            "\t\t/* --version */\n"
            "\t\tif (strcmp(arg, \"--version\") == 0) {\n"
            "\t\t\t%s_cmdline_version();\n"
            "\t\t\treturn 1;\n"
            "\t\t}\n\n",
            g->prefix, g->prefix);

    /* emit per-option matchers for top-level options */
    for (i = 0U; i < (unsigned int)fl.n; i++) {
        const flat_opt_t *e = &fl.entries[i];
        char long_with_dash[CF_MAX_IDENT_LEN + 2];
        (void)snprintf(long_with_dash, sizeof(long_with_dash), "--%s", e->long_name);

        /* long form: strncmp with optional '=' */
        fprintf(g->fc,
                "\t\tif (strncmp(arg, \"--%s\", %u) == 0 &&\n"
                "\t\t    (arg[%u] == '\\0' || arg[%u] == '=')) {\n",
                e->long_name,
                (unsigned int)(2U + strlen(e->long_name)),
                (unsigned int)(2U + strlen(e->long_name)),
                (unsigned int)(2U + strlen(e->long_name)));
        emit_value_parser(g, e);
        fprintf(g->fc, "\t\t}\n");

        /* short form */
        if (e->short_opt != '\0' && e->short_opt != '-') {
            const cf_type_expr_t *expr = resolve_alias(g->file, &e->opt->type);
            if (expr->base == CF_TYPE_FLAG) {
                fprintf(g->fc,
                        "\t\tif (strcmp(arg, \"-%c\") == 0) {\n"
                        "\t\t\tout->%s = 1;\n"
                        "\t\t\tcontinue;\n"
                        "\t\t}\n",
                        e->short_opt, e->struct_path);
            } else {
                /* reuse the same value-parser logic as the long-opt handler */
                fprintf(g->fc, "\t\tif (strcmp(arg, \"-%c\") == 0) {\n",
                        e->short_opt);
                emit_value_parser(g, e);
                fprintf(g->fc, "\t\t}\n");
            }
        }
    }

    /* subcommand-specific options */
    if (g->file->nsubcommands > 0U) {
        for (i = 0U; i < g->file->nsubcommands; i++) {
            const cf_subcommand_t *sub = &g->file->subcommands[i];
            char scname_c[CF_MAX_IDENT_LEN];
            char scname_u[CF_MAX_IDENT_LEN];
            flat_list_t sfl;
            unsigned int oi;
            unsigned int ssi;

            opt_c_name(scname_c, sub->name, sizeof(scname_c));
            str_upper(scname_u, sub->name, sizeof(scname_u));
            memset(&sfl, 0, sizeof(sfl));

            for (oi = 0U; oi < sub->noptions; oi++) flat_add(&sfl, &sub->options[oi], scname_c);
            for (ssi = 0U; ssi < sub->nsections; ssi++) {
                unsigned int soi;
                for (soi = 0U; soi < sub->sections[ssi].noptions; soi++) {
                    flat_add(&sfl, &sub->sections[ssi].options[soi], scname_c);
                }
            }

            if (sfl.n == 0U) continue;

            fprintf(g->fc, "\t\tif (out->subcmd == %s_CMD_%s) {\n",
                    g->PREFIX, scname_u);
            for (oi = 0U; oi < sfl.n; oi++) {
                const flat_opt_t *e = &sfl.entries[oi];
                unsigned int llen = (unsigned int)(2U + strlen(e->long_name));
                fprintf(g->fc,
                        "\t\t\tif (strncmp(arg, \"--%s\", %u) == 0 &&\n"
                        "\t\t\t    (arg[%u] == '\\0' || arg[%u] == '=')) {\n",
                        e->long_name, llen, llen, llen);
                emit_value_parser(g, e);
                fprintf(g->fc, "\t\t\t}\n");
            }
            fprintf(g->fc, "\t\t}\n");
        }
    }

    /* Emit pass-through checks for each @import alias.
     * --alias.* tokens belong to that lib's own parser; the main parser
     * must skip them silently rather than reject them as unknown. */
    if (g->file->nimports > 0U) {
        unsigned int im;
        fprintf(g->fc,
                "\t\t/* pass-through: options for @import namespaces */\n");
        for (im = 0U; im < g->file->nimports; im++) {
            const char *alias = g->file->imports[im].alias;
            /* emit: if (strncmp(arg, "--alias.", N) == 0) { continue; } */
            fprintf(g->fc,
                    "\t\tif (strncmp(arg, \"--%s.\", %u) == 0) { continue; }\n",
                    alias,
                    (unsigned int)(strlen(alias) + 3U)); /* "--" + alias + "." */
        }
    }
    /* Any remaining --X.Y token is a namespaced option for a dlopen plugin
     * or future import.  Pass through silently so the plugin's own parser
     * can handle it via cf_argv_slice(). */
    fprintf(g->fc,
            "\t\t/* pass-through: --namespace.option for dlopen plugins */\n"
            "\t\tif (arg[0]=='-' && arg[1]=='-' && strchr(arg+2,'.')!=NULL)"
            " { continue; }\n");
    fprintf(g->fc,
            "\t\t/* positional or unknown option */\n"
            "\t\tif (arg[0] == '-') {\n"
            "\t\t\tfprintf(stderr, \"error: unknown option: %%s\\n\", arg);\n"
            "\t\t\treturn -1;\n"
            "\t\t}\n"
            "\t}\n\n"
            "\treturn 0;\n"
            "}\n\n");

    /* positional parsing for subcommands */
    if (g->file->nsubcommands > 0U) {
        for (i = 0U; i < g->file->nsubcommands; i++) {
            const cf_subcommand_t *sub = &g->file->subcommands[i];
            unsigned int pi;
            if (sub->npositionals == 0U) continue;
            /* emit a helper */
            char scname_c[CF_MAX_IDENT_LEN];
            char scname_u[CF_MAX_IDENT_LEN];
            opt_c_name(scname_c, sub->name, sizeof(scname_c));
            str_upper(scname_u, sub->name, sizeof(scname_u));
            (void)pi;
        }
    }

    /* --- help function: emit each line as fputs to stay under C89 509-char limit --- */
    fprintf(g->fc, "void %s_cmdline_help(void)\n{\n", g->prefix);
    /* usage line */
    if (g->file->nsubcommands > 0U) {
        fprintf(g->fc, "\tfputs(\"Usage: %s [OPTIONS] <subcommand>\\n\", stdout);\n",
                g->app);
    } else {
        fprintf(g->fc, "\tfputs(\"Usage: %s [OPTIONS]\\n\", stdout);\n",
                g->app);
    }
    if (g->file->meta.brief[0] != '\0') {
        fprintf(g->fc, "\tfputs(\"%s\\n\", stdout);\n",
                g->file->meta.brief);
    }
    fprintf(g->fc, "\tfputs(\"\\nOptions:\\n\", stdout);\n");
    /* section options */
    for (si = 0U; si < g->file->nsections; si++) {
        const cf_section_t *sec2 = &g->file->sections[si];
        unsigned int oi2;
        if (sec2->name[0] != '\0') {
            fprintf(g->fc, "\tfputs(\"\\n  %s:\\n\", stdout);\n", sec2->name);
        }
        for (oi2 = 0U; oi2 < sec2->noptions; oi2++) {
            const cf_option_t *opt2 = &sec2->options[oi2];
            char sopt2[8];
            if (opt2->visible == CF_VIS_NEVER) continue;
            if (opt2->visible == CF_VIS_DETAIL) continue;
            if (opt2->short_opt != '\0' && opt2->short_opt != '-') {
                (void)snprintf(sopt2, sizeof(sopt2), "-%c,", opt2->short_opt);
            } else {
                cf_strlcpy(sopt2, "   ", sizeof(sopt2));
            }
            {
                char dflt_[72];
                char ehelp_[512];
                char edflt_[128];
                const cf_type_expr_t *ex_ = resolve_alias(g->file, &opt2->type);
                const char *hint_ = type_hint_str(ex_);
                fmt_help_default(dflt_, sizeof(dflt_), opt2, ex_);
                c_str_escape(ehelp_, sizeof(ehelp_),
                             (opt2->help[0] != '\0') ? opt2->help : "");
                c_str_escape(edflt_, sizeof(edflt_), dflt_);
                if (g->file->schema_version >= 2U && ex_->nunits > 0U) {
                    char uh_[80];
                    fmt_units_hint(uh_, sizeof(uh_), ex_);
                    (void)strncat(ehelp_, uh_,
                                  sizeof(ehelp_) - strlen(ehelp_) - 1U);
                }
                fprintf(g->fc,
                        "\tfputs(\"  %s --%-20s %-8s %s%s\\n\", stdout);\n",
                        sopt2, opt2->name, hint_, ehelp_, edflt_);
            }
        }
    }
    /* top-level options */
    for (i = 0U; i < g->file->noptions; i++) {
        const cf_option_t *opt2 = &g->file->options[i];
        char sopt2[8];
        if (opt2->visible == CF_VIS_NEVER) continue;
        if (opt2->visible == CF_VIS_DETAIL) continue;
        if (opt2->short_opt != '\0' && opt2->short_opt != '-') {
            (void)snprintf(sopt2, sizeof(sopt2), "-%c,", opt2->short_opt);
        } else {
            cf_strlcpy(sopt2, "   ", sizeof(sopt2));
        }
        {
            char dflt_[72];
            char ehelp_[512];
            char edflt_[128];
            const cf_type_expr_t *ex_ = resolve_alias(g->file, &opt2->type);
            const char *hint_ = type_hint_str(ex_);
            fmt_help_default(dflt_, sizeof(dflt_), opt2, ex_);
            c_str_escape(ehelp_, sizeof(ehelp_),
                         (opt2->help[0] != '\0') ? opt2->help : "");
            c_str_escape(edflt_, sizeof(edflt_), dflt_);
            if (g->file->schema_version >= 2U && ex_->nunits > 0U) {
                char uh_[80];
                fmt_units_hint(uh_, sizeof(uh_), ex_);
                (void)strncat(ehelp_, uh_,
                              sizeof(ehelp_) - strlen(ehelp_) - 1U);
            }
            fprintf(g->fc,
                    "\tfputs(\"  %s --%-20s %-8s %s%s\\n\", stdout);\n",
                    sopt2, opt2->name, hint_, ehelp_, edflt_);
        }
    }
    /* @import library options -------------------------------------------- *
     * Emit a sub-section in --help for every resolved @import schema so    *
     * the user can see each lib's options namespaced as --alias.option.    *
     * Uses the same visibility rules as the main help loop:                *
     *   CF_VIS_DETAIL -> skip (shown only under --help-detail)             *
     *   CF_VIS_NEVER  -> skip (never shown)                                *
     * -------------------------------------------------------------------- */
    if (g->opts != NULL && g->opts->nimport_schemas > 0U) {
        unsigned int ii;
        for (ii = 0U; ii < g->opts->nimport_schemas; ii++) {
            const cf_schema_file_t *imp    = g->opts->import_schemas[ii];
            const char             *ialias = g->opts->import_aliases[ii];
            unsigned int            si2, oi2;

            if (imp == NULL || ialias == NULL) continue;

            /* lib section header — tells user the exact syntax to use.
             * Emits e.g.:  fputs("\narith options (supply as --arith.OPTION=VALUE):\n", stdout);
             */
            fprintf(g->fc,
                    "\tfputs(\"\\n%s options"
                    " (supply as --%s.OPTION=VALUE):\\n\", stdout);\n",
                    ialias, ialias);

            /* options inside each section of the imported schema */
            for (si2 = 0U; si2 < imp->nsections; si2++) {
                const cf_section_t *sec2 = &imp->sections[si2];

                if (sec2->name[0] != '\0') {
                    fprintf(g->fc,
                            "\tfputs(\"  %s:\\n\", stdout);\n",
                            sec2->name);
                }
                for (oi2 = 0U; oi2 < sec2->noptions; oi2++) {
                    const cf_option_t    *opt3 = &sec2->options[oi2];
                    const cf_type_expr_t *ex3;
                    const char           *hint3;
                    char dflt3[128]; char ehelp3[512]; char edflt3[128];

                    if (opt3->visible == CF_VIS_DETAIL) continue;
                    if (opt3->visible == CF_VIS_NEVER)  continue;
                    ex3   = resolve_alias(imp, &opt3->type);
                    hint3 = type_hint_str(ex3);
                    fmt_help_default(dflt3, sizeof(dflt3), opt3, ex3);
                    c_str_escape(ehelp3, sizeof(ehelp3),
                                 (opt3->help[0] != '\0') ? opt3->help : "");
                    c_str_escape(edflt3, sizeof(edflt3), dflt3);
                    fprintf(g->fc,
                            "\tfputs(\"     --%s.%-17s %-8s %s%s\\n\","
                            " stdout);\n",
                            ialias, opt3->name, hint3, ehelp3, edflt3);
                }
            }

            /* top-level options of the imported schema (outside sections) */
            for (oi2 = 0U; oi2 < imp->noptions; oi2++) {
                const cf_option_t    *opt3 = &imp->options[oi2];
                const cf_type_expr_t *ex3;
                const char           *hint3;
                char dflt3[128]; char ehelp3[512]; char edflt3[128];

                if (opt3->visible == CF_VIS_DETAIL) continue;
                if (opt3->visible == CF_VIS_NEVER)  continue;
                ex3   = resolve_alias(imp, &opt3->type);
                hint3 = type_hint_str(ex3);
                fmt_help_default(dflt3, sizeof(dflt3), opt3, ex3);
                c_str_escape(ehelp3, sizeof(ehelp3),
                             (opt3->help[0] != '\0') ? opt3->help : "");
                c_str_escape(edflt3, sizeof(edflt3), dflt3);
                fprintf(g->fc,
                        "\tfputs(\"     --%s.%-17s %-8s %s%s\\n\","
                        " stdout);\n",
                        ialias, opt3->name, hint3, ehelp3, edflt3);
            }
        }
    }

    /* standard --help and --version */
    fprintf(g->fc, "\tfputs(\"\\n\", stdout);\n"); /* blank line after imports */
    fprintf(g->fc,
            "\tfputs(\"  %s --%-20s %-8s Show this help\\n\", stdout);\n"
            "\tfputs(\"  %s --%-20s %-8s Show version\\n\", stdout);\n",
            "   ", "help",    "",
            "   ", "version", "");
    /* subcommand list */
    if (g->file->nsubcommands > 0U) {
        fprintf(g->fc, "\tfputs(\"\\nSubcommands:\\n\", stdout);\n");
        for (i = 0U; i < g->file->nsubcommands; i++) {
            const cf_subcommand_t *sub2 = &g->file->subcommands[i];
            fprintf(g->fc, "\tfputs(\"  %-28s %s\\n\", stdout);\n",
                    sub2->name,
                    (sub2->brief[0] != '\0') ? sub2->brief : "");
        }
    }
    fprintf(g->fc, "}\n\n");

    /* --- version function --- */
    fprintf(g->fc,
            "void %s_cmdline_version(void)\n"
            "{\n"
            "\tfprintf(stdout, \"%%s\\n\", %s__version);\n"
            "}\n\n",
            g->prefix, g->prefix);

    /* --- dump function --- */
    fprintf(g->fc,
            "void %s_cmdline_dump(const struct %s_cmdline *args)\n"
            "{\n",
            g->prefix, g->prefix);
    if (g->file->schema_version >= 2U) {
        /* harmless under v2: guarantees `args` is used even when every
         * option is a compound printed as a placeholder. */
        fprintf(g->fc, "\t(void)args;\n");
    }

    for (i = 0U; i < (unsigned int)fl.n; i++) {
        const flat_opt_t  *e    = &fl.entries[i];
        const cf_option_t *opt  = e->opt;
        const cf_type_expr_t *expr = resolve_alias(g->file, &opt->type);

        if (expr->base == CF_TYPE_FLAG || expr->base == CF_TYPE_BOOL) {
            fprintf(g->fc, "\tfprintf(stdout, \"%s=%%d\\n\", args->%s);\n",
                    e->long_name, e->struct_path);
        } else if (expr->base == CF_TYPE_STRING || expr->base == CF_TYPE_PATH ||
                   expr->base == CF_TYPE_FILE   || expr->base == CF_TYPE_DIR) {
            if (opt->sensitive) {
                fprintf(g->fc, "\tfprintf(stdout, \"%s=***\\n\");\n", e->long_name);
            } else if (opt->multiple.enabled && opt->multiple.max > 1U) {
                /* multiple string field: print each element with index */
                fprintf(g->fc,
                        "\t{ int _di; for (_di = 0; _di < args->%s_count; _di++)\n"
                        "\t    fprintf(stdout, \"%s[%%d]=%%s\\n\", _di, args->%s[_di]); }\n",
                        e->struct_path, e->long_name, e->struct_path);
            } else {
                fprintf(g->fc, "\tfprintf(stdout, \"%s=%%s\\n\", args->%s);\n",
                        e->long_name, e->struct_path);
            }
        } else if (opt->multiple.enabled && opt->multiple.max > 1U) {
            /* multiple numeric/compound: just print count */
            fprintf(g->fc, "\tfprintf(stdout, \"%s_count=%%d\\n\", args->%s_count);\n",
                    e->long_name, e->struct_path);
        } else if (expr->base == CF_TYPE_COMPOUND) {
            /* inline compound struct: not scalar — skip individual dump */
            fprintf(g->fc, "\tfprintf(stdout, \"%s=<compound>\\n\");\n",
                    e->long_name);
        } else if (expr->base == CF_TYPE_FLOAT) {
            fprintf(g->fc, "\tfprintf(stdout, \"%s=%%g\\n\", (double)args->%s);\n",
                    e->long_name, e->struct_path);
        } else if (expr->base == CF_TYPE_DOUBLE) {
            fprintf(g->fc, "\tfprintf(stdout, \"%s=%%g\\n\", args->%s);\n",
                    e->long_name, e->struct_path);
        } else if ((expr->base == CF_TYPE_DURATION ||
                    expr->base == CF_TYPE_BYTES ||
                    expr->base == CF_TYPE_FREQUENCY) &&
                   g->file->schema_version >= 2U &&
                   qgroup_for(expr->base) != NULL) {
            const cf_qgroup_t *qg = qgroup_for(expr->base);
            fprintf(g->fc,
                    "\tfprintf(stdout, \"%s=%%lu %s\\n\", "
                    "(unsigned long)%s_%s_to_%s(&args->%s));\n",
                    e->long_name, qg->to_suffix,
                    g->prefix, qg->gname, qg->to_suffix, e->struct_path);
        } else {
            fprintf(g->fc, "\tfprintf(stdout, \"%s=%%ld\\n\", (long)args->%s);\n",
                    e->long_name, e->struct_path);
        }
    }

    fprintf(g->fc, "}\n");
}

/* =========================================================================
 * Markdown generation
 * ====================================================================== */

static void gen_markdown(gen_ctx_t *g)
{
    unsigned int i;
    unsigned int si;

    const char *title = (g->file->meta.doc_title[0] != '\0')
                        ? g->file->meta.doc_title
                        : g->app;
    fprintf(g->fmd, "# %s\n\n", title);

    if (g->file->meta.brief[0] != '\0') {
        fprintf(g->fmd, "%s\n\n", g->file->meta.brief);
    }
    if (g->file->meta.description[0] != '\0') {
        fprintf(g->fmd, "%s\n\n", g->file->meta.description);
    }
    if (g->file->meta.version[0] != '\0') {
        fprintf(g->fmd, "**Version:** %s", g->file->meta.version);
        if (g->file->meta.author[0] != '\0') {
            fprintf(g->fmd, "  **Author:** %s", g->file->meta.author);
        }
        fprintf(g->fmd, "\n\n");
    }

    fprintf(g->fmd, "## Options\n\n");

    for (si = 0U; si < g->file->nsections; si++) {
        const cf_section_t *sec = &g->file->sections[si];
        unsigned int oi;
        if (sec->name[0] != '\0') fprintf(g->fmd, "### %s\n\n", sec->name);
        if (sec->description[0] != '\0') fprintf(g->fmd, "%s\n\n", sec->description);
        for (oi = 0U; oi < sec->noptions; oi++) {
            const cf_option_t *opt = &sec->options[oi];
            fprintf(g->fmd, "#### `--%-s`\n\n", opt->name);
            if (opt->help[0] != '\0') fprintf(g->fmd, "%s\n\n", opt->help);
            if (opt->details[0] != '\0') fprintf(g->fmd, "%s\n\n", opt->details);
            if (opt->example[0] != '\0') fprintf(g->fmd, "**Example:** `%s`\n\n", opt->example);
            if (opt->since[0] != '\0') fprintf(g->fmd, "*Since %s*\n\n", opt->since);
        }
    }
    for (i = 0U; i < g->file->noptions; i++) {
        const cf_option_t *opt = &g->file->options[i];
        fprintf(g->fmd, "#### `--%-s`\n\n", opt->name);
        if (opt->help[0] != '\0') fprintf(g->fmd, "%s\n\n", opt->help);
        if (opt->details[0] != '\0') fprintf(g->fmd, "%s\n\n", opt->details);
    }

    if (g->file->nsubcommands > 0U) {
        fprintf(g->fmd, "## Subcommands\n\n");
        for (i = 0U; i < g->file->nsubcommands; i++) {
            const cf_subcommand_t *sub = &g->file->subcommands[i];
            unsigned int oi;
            unsigned int ssi;
            fprintf(g->fmd, "### `%s`\n\n", sub->name);
            if (sub->brief[0] != '\0') fprintf(g->fmd, "%s\n\n", sub->brief);
            if (sub->description[0] != '\0') fprintf(g->fmd, "%s\n\n", sub->description);
            if (sub->is_deprecated) {
                fprintf(g->fmd, "> **Deprecated:** %s\n\n", sub->deprecated);
            }
            for (oi = 0U; oi < sub->noptions; oi++) {
                const cf_option_t *opt = &sub->options[oi];
                fprintf(g->fmd, "- `--%-s` — %s\n", opt->name, opt->help);
            }
            for (ssi = 0U; ssi < sub->nsections; ssi++) {
                unsigned int soi;
                for (soi = 0U; soi < sub->sections[ssi].noptions; soi++) {
                    const cf_option_t *opt = &sub->sections[ssi].options[soi];
                    fprintf(g->fmd, "- `--%-s` — %s\n", opt->name, opt->help);
                }
            }
            fprintf(g->fmd, "\n");
        }
    }

    /* @import library options section in the markdown reference ----------- *
     * Tells the app author exactly which options each imported lib exposes, *
     * the prefix to use on the command line, and what they must wire up     *
     * (cf_argv_slice + lib init) for the options to take effect.            *
     * -------------------------------------------------------------------- */
    if (g->opts != NULL && g->opts->nimport_schemas > 0U) {
        unsigned int ii;
        fprintf(g->fmd,
                "## Imported Library Options\n\n"
                "The following options belong to libraries imported with "
                "`@import`.  They are passed through by the generated parser "
                "unchanged and must be forwarded to each library\'s own "
                "parser by the host application using `cf_argv_slice()`.\n\n"
                "```c\n"
                "#include <cliforge/cf_argv_slice.h>\n\n"
                "char  *lib_argv[CF_SLICE_MAX_ARGS];\n"
                "char   lib_buf[CF_SLICE_STRBUF];\n"
                "int    lib_argc = cf_argv_slice(\"<alias>\",\n"
                "                               argc, argv,\n"
                "                               lib_argv, CF_SLICE_MAX_ARGS,\n"
                "                               lib_buf,  CF_SLICE_STRBUF);\n"
                "/* then pass lib_argc / lib_argv to the lib\'s init() */\n"
                "```\n\n");

        for (ii = 0U; ii < g->opts->nimport_schemas; ii++) {
            const cf_schema_file_t *imp    = g->opts->import_schemas[ii];
            const char             *ialias = g->opts->import_aliases[ii];
            unsigned int            si2, oi2;

            if (imp == NULL || ialias == NULL) continue;

            fprintf(g->fmd,
                    "### `%s` — %s\n\n"
                    "Supply on the command line as `--%s.OPTION=VALUE`.\n"
                    "Forward to the library with `cf_argv_slice(\"%s\", ...)`"
                    " before calling the library\'s init function.\n\n"
                    "| Option | Type | Default | Description |\n"
                    "|--------|------|---------|-------------|\n",
                    ialias,
                    (imp->meta.brief[0] != '\0') ? imp->meta.brief
                                                   : "imported library",
                    ialias, ialias);

            /* section options */
            for (si2 = 0U; si2 < imp->nsections; si2++) {
                const cf_section_t *sec2 = &imp->sections[si2];
                for (oi2 = 0U; oi2 < sec2->noptions; oi2++) {
                    const cf_option_t    *opt3 = &sec2->options[oi2];
                    const cf_type_expr_t *ex3  = resolve_alias(imp, &opt3->type);
                    const char           *hint3 = type_hint_str(ex3);
                    if (opt3->visible == CF_VIS_NEVER) continue;
                    fprintf(g->fmd, "| `--%s.%s` | %s | `%s` | %s |\n",
                            ialias, opt3->name, hint3,
                            (opt3->default_val[0] != '\0') ? opt3->default_val : "—",
                            (opt3->help[0] != '\0') ? opt3->help : "");
                }
            }
            /* top-level options */
            for (oi2 = 0U; oi2 < imp->noptions; oi2++) {
                const cf_option_t    *opt3 = &imp->options[oi2];
                const cf_type_expr_t *ex3  = resolve_alias(imp, &opt3->type);
                const char           *hint3 = type_hint_str(ex3);
                if (opt3->visible == CF_VIS_NEVER) continue;
                fprintf(g->fmd, "| `--%s.%s` | %s | `%s` | %s |\n",
                        ialias, opt3->name, hint3,
                        (opt3->default_val[0] != '\0') ? opt3->default_val : "—",
                        (opt3->help[0] != '\0') ? opt3->help : "");
            }
            fprintf(g->fmd, "\n");
        }
    }
}

/* =========================================================================
 * Public entry point
 * ====================================================================== */

int cf_generate(const cf_schema_file_t *file, const cf_gen_options_t *opts)
{
    gen_ctx_t  g;
    char       hpath[1024];
    char       cpath[1024];
    char       mdpath[1024];
    const char *dir;
    int        rc = 0;

    memset(&g, 0, sizeof(g));
    g.file = file;
    g.opts = opts;

    /* Derive prefix, output base name, app name */
    if (file->meta.prefix[0] != '\0') {
        cf_strlcpy(g.prefix, file->meta.prefix, sizeof(g.prefix));
    } else if (file->meta.app[0] != '\0') {
        opt_c_name(g.prefix, file->meta.app, sizeof(g.prefix));
    } else {
        cf_strlcpy(g.prefix, "cf", sizeof(g.prefix));
    }
    str_upper(g.PREFIX, g.prefix, sizeof(g.PREFIX));

    if (file->meta.output[0] != '\0') {
        cf_strlcpy(g.output, file->meta.output, sizeof(g.output));
    } else {
        cf_strlcpy(g.output, "cmdline", sizeof(g.output));
    }

    if (file->meta.app[0] != '\0') {
        cf_strlcpy(g.app, file->meta.app, sizeof(g.app));
    } else {
        cf_strlcpy(g.app, g.prefix, sizeof(g.app));
    }

    dir = (opts->output_dir != NULL && opts->output_dir[0] != '\0')
          ? opts->output_dir : ".";

    (void)snprintf(hpath,  sizeof(hpath),  "%s/%s.h",  dir, g.output);
    (void)snprintf(cpath,  sizeof(cpath),  "%s/%s.c",  dir, g.output);
    (void)snprintf(mdpath, sizeof(mdpath), "%s/%s.md", dir, g.output);

    if (opts->dry_run) {
        g.fh  = stdout;
        g.fc  = stdout;
        g.fmd = stdout;
        fprintf(stdout, "\n/* === dry-run: %s.h + %s.c + %s.md === */\n",
                g.output, g.output, g.output);
    } else {
        g.fh = fopen(hpath, "w");
        if (g.fh == NULL) {
            fprintf(stderr, "cliforge: cannot open '%s' for writing\n", hpath);
            return -1;
        }
        g.fc = fopen(cpath, "w");
        if (g.fc == NULL) {
            fprintf(stderr, "cliforge: cannot open '%s' for writing\n", cpath);
            (void)fclose(g.fh);
            return -1;
        }
        g.fmd = fopen(mdpath, "w");
        if (g.fmd == NULL) {
            fprintf(stderr, "cliforge: cannot open '%s' for writing\n", mdpath);
            (void)fclose(g.fh);
            (void)fclose(g.fc);
            return -1;
        }
    }

    gen_header(&g);
    gen_source(&g);
    gen_markdown(&g);

    if (!opts->dry_run) {
        (void)fclose(g.fh);
        (void)fclose(g.fc);
        (void)fclose(g.fmd);
        if (opts->verbose) {
            fprintf(stdout, "cliforge: wrote %s  %s  %s\n",
                    hpath, cpath, mdpath);
        }
    }

    (void)rc;
    return g.had_error ? -1 : 0;
}
