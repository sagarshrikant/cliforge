/* ============================================================================
 * cf_ast.h - cliforge Abstract Syntax Tree node types
 *
 * All AST nodes use fixed-size arrays; counts are recorded separately.
 * ========================================================================= */

#ifndef CF_AST_H
#define CF_AST_H

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Limits
 *
 * Size limits: chosen so that sizeof(cf_schema_file_t) stays under ~25 MB
 * (safe for a single heap allocation).  If your schema exceeds a limit,
 * raise the relevant constant and rebuild cliforge -- the limits are
 * compile-time constants and do not affect generated code size.
 *
 * Rule of thumb:
 *   CF_MAX_IDENT_LEN  - option/type/app names (short identifiers)
 *   CF_MAX_STRING_LEN - doc strings (help, details, note, example, brief)
 *   Array limits       - kept small because they nest three levels deep
 * ---------------------------------------------------------------------- */

#define CF_MAX_IDENT_LEN    64
#define CF_MAX_STRING_LEN   512
#define CF_MAX_CHILDREN     64
#define CF_MAX_FIELDS       8
#define CF_MAX_MEMBERS      16
#define CF_MAX_OPTIONS      32
#define CF_MAX_SECTIONS     16
#define CF_MAX_IMPORTS      16
#define CF_MAX_NAMED_TYPES  16
#define CF_MAX_SUBCOMMANDS  8
#define CF_MAX_POSITIONALS  8
#define CF_MAX_GROUPS       8
#define CF_MAX_ERRORS       64
#define CF_MAX_KV           16     /* key-value pairs in meta/option block */

/* -------------------------------------------------------------------------
 * Type system
 * ---------------------------------------------------------------------- */

typedef enum cf_base_type {
    CF_TYPE_NONE      = 0,
    /* signed integers */
    CF_TYPE_SINT8,
    CF_TYPE_SINT16,
    CF_TYPE_SINT32,
    CF_TYPE_SINT64,
    /* unsigned integers */
    CF_TYPE_UINT8,
    CF_TYPE_UINT16,
    CF_TYPE_UINT32,
    CF_TYPE_UINT64,
    /* floating-point */
    CF_TYPE_FLOAT,
    CF_TYPE_DOUBLE,
    /* logical */
    CF_TYPE_BOOL,
    CF_TYPE_FLAG,
    /* string/path types */
    CF_TYPE_STRING,
    CF_TYPE_PATH,
    CF_TYPE_FILE,
    CF_TYPE_DIR,
    /* unit-aware quantities */
    CF_TYPE_DURATION,
    CF_TYPE_BYTES,
    CF_TYPE_FREQUENCY,
    CF_TYPE_RATIO,
    /* composite */
    CF_TYPE_CHOICE,    /* inline (A, B, C) or named alias */
    CF_TYPE_COMPOUND,  /* inline { f: t, ... } or named alias */
    /* named alias (resolved to CHOICE or COMPOUND during validation) */
    CF_TYPE_ALIAS
} cf_base_type_t;

/* A compound field: name + type */
typedef struct cf_field {
    char            name[CF_MAX_IDENT_LEN];
    cf_base_type_t  base;
    char            alias_name[CF_MAX_IDENT_LEN]; /* if base == CF_TYPE_ALIAS */
    unsigned int    str_len;   /* for string/path/file/dir: buffer length    */
    /* default value for this field */
    char            default_val[CF_MAX_IDENT_LEN];
    int             has_default;
} cf_field_t;

/* A type expression: base type plus parameters */
typedef struct cf_type_expr {
    cf_base_type_t  base;

    /* CF_TYPE_ALIAS -- name of a declared named type */
    char            alias_name[CF_MAX_IDENT_LEN];

    /* CF_TYPE_STRING / PATH / FILE / DIR */
    unsigned int    str_len;   /* buffer length, default 256 */

    /* CF_TYPE_CHOICE (inline) */
    char            members[CF_MAX_MEMBERS][CF_MAX_IDENT_LEN];
    unsigned int    nmembers;

    /* CF_TYPE_COMPOUND (inline) */
    cf_field_t      fields[CF_MAX_FIELDS];
    unsigned int    nfields;

    /* range constraint: has_range=1 means value must be in [lo, hi] */
    int             has_range;
    char            range_lo[CF_MAX_IDENT_LEN];
    char            range_hi[CF_MAX_IDENT_LEN];

    /* v2 `units [..]` — accepted unit suffixes for a quantity type.
     * nunits == 0 means "all units of the group are accepted". */
    char            units[CF_MAX_MEMBERS][8];
    unsigned int    nunits;
} cf_type_expr_t;

/* -------------------------------------------------------------------------
 * Named type declaration   name = (...) or name = { ... }
 * ---------------------------------------------------------------------- */

typedef struct cf_named_type {
    char            name[CF_MAX_IDENT_LEN];
    cf_type_expr_t  expr;
    unsigned int    line;
} cf_named_type_t;

/* -------------------------------------------------------------------------
 * Option / positional qualifiers
 * ---------------------------------------------------------------------- */

typedef enum cf_on_error {
    CF_ONERR_DEFAULT = 0,  /* unset: inherit meta default, else exit */
    CF_ONERR_EXIT    = 1,  /* bad value: report and fail the parse    */
    CF_ONERR_WARN    = 2   /* bad value: warn, keep default, continue */
} cf_on_error_t;

typedef enum cf_required {
    CF_REQ_OPTIONAL  = 0,
    CF_REQ_MANDATORY = 1
} cf_required_t;

typedef enum cf_visible {
    CF_VIS_ALL    = 0,
    CF_VIS_DETAIL = 1,
    CF_VIS_NEVER  = 2
} cf_visible_t;

typedef struct cf_multiple {
    int          enabled;
    unsigned int min;   /* 0 if no explicit min */
    unsigned int max;   /* 0 means not specified (treated as 1) */
} cf_multiple_t;

/* -------------------------------------------------------------------------
 * Option declaration
 * ---------------------------------------------------------------------- */

typedef struct cf_option {
    char           name[CF_MAX_IDENT_LEN];
    cf_type_expr_t type;
    char           short_opt;     /* '\0' = none */
    char           default_val[CF_MAX_STRING_LEN];
    int            has_default;
    cf_required_t  required;
    cf_visible_t   visible;
    cf_multiple_t  multiple;
    char           alias_name[CF_MAX_IDENT_LEN];
    char           depends_on[CF_MAX_IDENT_LEN];
    char           conflicts[CF_MAX_IDENT_LEN];
    int            sensitive;
    int            unique;
    char           deprecated[CF_MAX_STRING_LEN];
    int            is_deprecated;
    char           display_unit[CF_MAX_IDENT_LEN];
    cf_on_error_t  on_error;      /* validation failure policy (v2) */
    /* documentation */
    char           help[CF_MAX_STRING_LEN];
    char           details[CF_MAX_STRING_LEN];
    char           note[CF_MAX_STRING_LEN];
    char           example[CF_MAX_STRING_LEN];
    char           since[CF_MAX_IDENT_LEN];
    /* source location */
    unsigned int   line;
} cf_option_t;

/* -------------------------------------------------------------------------
 * Section block
 * ---------------------------------------------------------------------- */

typedef struct cf_section {
    char           name[CF_MAX_STRING_LEN]; /* optional display name */
    char           description[CF_MAX_STRING_LEN];
    /* named types declared inside this section */
    cf_named_type_t named_types[CF_MAX_NAMED_TYPES];
    unsigned int    nnamed_types;
    /* options in this section */
    cf_option_t    options[CF_MAX_OPTIONS];
    unsigned int   noptions;
    unsigned int   line;
} cf_section_t;

/* -------------------------------------------------------------------------
 * Group block
 * ---------------------------------------------------------------------- */

typedef struct cf_group {
    char           name[CF_MAX_IDENT_LEN];
    char           members[CF_MAX_MEMBERS][CF_MAX_IDENT_LEN];
    unsigned int   nmembers;
    int            mandatory;  /* exactly-one semantics */
    unsigned int   line;
} cf_group_t;

/* -------------------------------------------------------------------------
 * Positional argument block
 * ---------------------------------------------------------------------- */

typedef struct cf_positional {
    char           name[CF_MAX_IDENT_LEN];
    cf_type_expr_t type;
    cf_required_t  required;
    cf_multiple_t  multiple;
    char           help[CF_MAX_STRING_LEN];
    char           details[CF_MAX_STRING_LEN];
    unsigned int   line;
} cf_positional_t;

/* -------------------------------------------------------------------------
 * Subcommand block
 * ---------------------------------------------------------------------- */

typedef struct cf_subcommand {
    char              name[CF_MAX_IDENT_LEN];
    char              brief[CF_MAX_STRING_LEN];
    char              description[CF_MAX_STRING_LEN];
    char              deprecated[CF_MAX_STRING_LEN];
    int               is_deprecated;
    /* sections inside the subcommand */
    cf_section_t      sections[CF_MAX_SECTIONS];
    unsigned int      nsections;
    /* top-level options inside the subcommand */
    cf_option_t       options[CF_MAX_OPTIONS];
    unsigned int      noptions;
    /* positionals */
    cf_positional_t   positionals[CF_MAX_POSITIONALS];
    unsigned int      npositionals;
    /* groups */
    cf_group_t        groups[CF_MAX_GROUPS];
    unsigned int      ngroups;
    unsigned int      line;
} cf_subcommand_t;

/* -------------------------------------------------------------------------
 * @import directive
 * ---------------------------------------------------------------------- */

typedef enum cf_import_cond_kind {
    CF_IMPORT_COND_NONE = 0,
    CF_IMPORT_COND_IFKEY,
    CF_IMPORT_COND_IFNKEY
} cf_import_cond_kind_t;

typedef struct cf_import {
    char                   path[CF_MAX_STRING_LEN];
    char                   alias[CF_MAX_IDENT_LEN];
    cf_import_cond_kind_t  cond_kind;
    char                   cond_key[CF_MAX_IDENT_LEN];
    char                   cond_op[4];    /* "==" or "!=" or "" */
    char                   cond_val[CF_MAX_IDENT_LEN];
    unsigned int           line;
} cf_import_t;

/* -------------------------------------------------------------------------
 * Meta block
 * ---------------------------------------------------------------------- */

typedef struct cf_meta {
    char  app[CF_MAX_IDENT_LEN];
    char  brief[CF_MAX_STRING_LEN];
    char  version[CF_MAX_IDENT_LEN];
    char  author[CF_MAX_STRING_LEN];
    char  prefix[CF_MAX_IDENT_LEN];
    char  output[CF_MAX_IDENT_LEN];
    char  doc_title[CF_MAX_STRING_LEN];
    char  description[CF_MAX_STRING_LEN];
    char  i18n[CF_MAX_STRING_LEN];
    cf_on_error_t on_error;   /* project-wide default validation policy */
    int   present;
} cf_meta_t;

/* -------------------------------------------------------------------------
 * Conditional block  (ifdef / ifndef / ifkey / ifnkey)
 * ---------------------------------------------------------------------- */

typedef enum cf_cond_kind {
    CF_COND_IFDEF  = 0,
    CF_COND_IFNDEF = 1,
    CF_COND_IFKEY  = 2,
    CF_COND_IFNKEY = 3
} cf_cond_kind_t;

/* A conditional block wraps sections / options / named-types. We record
 * the condition expression but for v1 code generation we emit the contents
 * unconditionally (the build system handles the filtering). */
typedef struct cf_conditional {
    cf_cond_kind_t  kind;
    char            symbol[CF_MAX_IDENT_LEN];  /* ifdef/ifndef */
    char            key[CF_MAX_IDENT_LEN];     /* ifkey/ifnkey */
    char            op[4];
    char            val[CF_MAX_IDENT_LEN];
    /* child sections / options flat-merged into the parent by the parser */
} cf_conditional_t;

/* -------------------------------------------------------------------------
 * Top-level schema file node
 * ---------------------------------------------------------------------- */

typedef struct cf_schema_file {
    /* @schema directive */
    int            schema_present;
    unsigned int   schema_version; /* 1 */

    /* @import directives */
    cf_import_t    imports[CF_MAX_IMPORTS];
    unsigned int   nimports;

    /* meta block */
    cf_meta_t      meta;

    /* top-level named type declarations */
    cf_named_type_t named_types[CF_MAX_NAMED_TYPES];
    unsigned int    nnamed_types;

    /* sections */
    cf_section_t   sections[CF_MAX_SECTIONS];
    unsigned int   nsections;

    /* top-level options (outside any section) */
    cf_option_t    options[CF_MAX_OPTIONS];
    unsigned int   noptions;

    /* groups */
    cf_group_t     groups[CF_MAX_GROUPS];
    unsigned int   ngroups;

    /* subcommands */
    cf_subcommand_t subcommands[CF_MAX_SUBCOMMANDS];
    unsigned int    nsubcommands;

    /* positionals at top level */
    cf_positional_t positionals[CF_MAX_POSITIONALS];
    unsigned int    npositionals;

    /* source filename */
    char           filename[CF_MAX_STRING_LEN];
} cf_schema_file_t;

#endif /* CF_AST_H */
