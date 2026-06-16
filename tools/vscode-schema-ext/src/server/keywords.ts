/* ============================================================================
 *  keywords.ts
 *  ----------------------------------------------------------------------------
 *  Canonical lists of identifiers reserved by the cliforge schema language.
 *  These tables drive lexing, completion, hover, and reserved-keyword checks.
 *
 *  Keep sorted by category in the same order SPEC.md lists them.
 *  When the spec changes, this file is the single source of truth for the LSP.
 * ========================================================================== */

/** Block-introducing top-level keywords (followed by an identifier or block). */
export const BLOCK_KEYWORDS = new Set<string>([
    "meta",
    "section",
    "option",
    "group",
    "subcommand",
    "positional",
    "i18n",
]);

/**
 * Conditional-block keywords.
 * Each opens a bare block `{ ... }` that may contain sections and options.
 *   - ifdef / ifndef   : build-time; reads CFLAGS / CPPFLAGS for -DSYMBOL
 *   - ifkey / ifnkey   : build-time; reads --key-words= list (or KEY==VALUE)
 */
export const CONDITIONAL_KEYWORDS = new Set<string>([
    "ifdef",
    "ifndef",
    "ifkey",
    "ifnkey",
]);

/** Directives — always introduced with a leading `@`. */
export const DIRECTIVES = new Set<string>([
    "@schema",
    "@import",
]);

/** Option / field qualifier keys (left-hand sides inside `option { ... }`). */
export const QUALIFIER_KEYS = new Set<string>([
    "type",
    "short",
    "default",
    "required",
    "visible",
    "multiple",
    "alias",
    "depends-on",
    "conflicts",
    "allowed",
    "sensitive",
    "deprecated",
    "display-unit",
    "unique",
    "help",
    "details",
    "note",
    "example",
    "since",
    "on-error",
    "units",
]);

/** Meta-block keys. */
export const META_KEYS = new Set<string>([
    "app",
    "brief",
    "version",
    "author",
    "prefix",
    "output",
    "doc-title",
    "description",
    "i18n",
    "on-error",
]);

/** `required` qualifier values. */
export const REQUIRED_VALUES = new Set<string>(["optional", "mandatory"]);

/** `visible` qualifier values. */
export const VISIBLE_VALUES = new Set<string>(["all", "detail", "never"]);

/** Primitive type names (no C-specific enum/struct keywords). */
export const PRIMITIVE_TYPES = new Set<string>([
    "sint8", "sint16", "sint32", "sint64",
    "uint8", "uint16", "uint32", "uint64",
    "float", "double",
    "bool", "flag",
    "string", "path", "file", "dir",
]);

/** Unit-aware (quantity) type constructors. */
export const QUANTITY_TYPES = new Set<string>([
    "duration",
    "bytes",
    "frequency",
    "ratio",
]);

/** All recognized unit suffixes (across every quantity group). */
export const UNIT_SUFFIXES = new Set<string>([
    "ns", "us", "µs", "ms", "s", "m", "h", "d",
    "B", "KB", "KiB", "MB", "MiB", "GB", "GiB", "TB", "TiB",
    "Hz", "kHz", "MHz", "GHz",
    "%",
]);

/** Unit groups — used by the validator to type-check defaults vs target. */
export const UNIT_GROUP: Record<string, "duration" | "bytes" | "frequency" | "ratio"> = {
    "ns": "duration", "us": "duration", "µs": "duration", "ms": "duration",
    "s": "duration", "m": "duration", "h": "duration", "d": "duration",
    "B": "bytes", "KB": "bytes", "KiB": "bytes",
    "MB": "bytes", "MiB": "bytes", "GB": "bytes", "GiB": "bytes",
    "TB": "bytes", "TiB": "bytes",
    "Hz": "frequency", "kHz": "frequency", "MHz": "frequency", "GHz": "frequency",
    "%": "ratio",
};

/** Boolean literals. */
export const BOOLEAN_LITERALS = new Set<string>(["true", "false"]);

/** `group` field keys. */
export const GROUP_KEYS = new Set<string>(["options", "mandatory"]);

/**
 * Reserved tokens that cannot be used as user identifiers
 * (option names, choice member names, type alias names, etc.).
 */
export const RESERVED_KEYWORDS = new Set<string>([
    // Block keywords
    "meta", "section", "option", "group", "subcommand", "positional", "i18n",
    // Conditional keywords
    "ifdef", "ifndef", "ifkey", "ifnkey",
    // Qualifier keys
    "type", "short", "default", "required", "visible", "multiple",
    "alias", "depends-on", "conflicts", "allowed", "sensitive", "deprecated",
    "display-unit", "unique", "help", "details", "note", "example", "since",
    "on-error", "units",
    // Meta keys
    "app", "brief", "version", "author", "prefix", "output",
    "doc-title", "description",
    // Built-in values
    "true", "false",
    "optional", "mandatory",
    "all", "detail", "never",
    // Primitive types
    "sint8", "sint16", "sint32", "sint64",
    "uint8", "uint16", "uint32", "uint64",
    "float", "double", "bool", "flag",
    "string", "path", "file", "dir",
    // Quantity types
    "duration", "bytes", "frequency", "ratio",
    // Directives / import clause keywords
    "as", "ifkey",
    // Reserved for future use
    "dynamic",
]);

/**
 * Keywords that are reserved as compound-field names.
 * This is deliberately narrower than RESERVED_KEYWORDS: primitive type names
 * (string, path, file, dir, uint8 …) are allowed as field names inside a
 * compound type because `{ path: file, name: string }` is idiomatic cliforge.
 * Only structural / block keywords are truly off-limits in that context.
 */
export const FIELD_RESERVED_KEYWORDS = new Set<string>([
    "meta", "section", "option", "group", "subcommand", "positional",
    "ifdef", "ifndef", "ifkey", "ifnkey",
    "default", "type", "true", "false",
    "as", "dynamic",
]);

/* ----------------------------------------------------------------------------
 * Human-readable documentation strings used by hover & completion.
 * Keys map to the canonical token (case-sensitive) the LSP looks up.
 * -------------------------------------------------------------------------- */

export const KEYWORD_DOCS: Record<string, string> = {
    "@schema": "**Schema directive.** Must be the first non-comment tokens in every `.cf` file. Anchors the parser to a known grammar revision.\n\n```\n@schema cliforge v1\n```",
    "@import": "**Import another schema.** Brings another module's option surface into scope under a mandatory alias. The alias becomes the dotted prefix on the CLI (`--alias.option`).\n\n```\n@import \"lib/trig.cf\" as trig\n@import \"lib/trig.cf\" as trig   ifkey have-trig\n```",

    "meta":    "**Meta block.** Schema-level metadata: app name, brief, version, author, C symbol prefix, generated file basename, and optional documentation fields.",
    "section": "**Section block.** Visual grouping for `--help` output. Sections do **not** introduce a namespace. May contain named type declarations (`name = (...)` or `name = { ... }`) as well as options.",
    "option":  "**Option declaration.** Defines a single command-line option with its type, default, validators, repetition policy, and documentation fields.",
    "group":   "**Validation group.** Cross-option mutual exclusion. `mandatory` makes exactly-one required; without `mandatory`, the group is advisory (documents related options).",
    "subcommand": "**Subcommand block.** Git-style nested option surface (e.g. `tool eval ...`, `tool bench ...`). The first non-option argv element selects the subcommand.",
    "positional": "**Positional argument block.** Declares order and type of positional argv values. Use `multiple = N` or `multiple = min..max` for variadic positions.",

    "ifdef":  "**Build-time conditional block.** Content is included only when the symbol is defined in `CFLAGS`/`CPPFLAGS`. Supports `||` and `&&`.\n\n```\nifdef LINUX_BUILD { ... }\nifdef LINUX_BUILD || RTOS_BUILD { ... }\n```",
    "ifndef": "**Build-time conditional block.** Inverse of `ifdef` — content included when the symbol is *not* defined.\n\n```\nifndef EMBEDDED { ... }\n```",
    "ifkey":  "**Key-words conditional block.** Content included when the key (and optional value) appears in `--key-words=...`.\n\n```\nifkey have-trig { ... }\nifkey board == rpi4 { ... }\n```",
    "ifnkey": "**Key-words conditional block.** Inverse of `ifkey` — content included when the key is *not* in `--key-words=...`.",

    // Option qualifier keys
    "type":         "Declares the option's type. May be a primitive, a quantity type, an inline choice `(A, B, C)`, an inline compound `{ field: type }`, or the name of a previously declared type alias.",
    "short":        "Single-character short option alias, e.g. `short = 'v'`. Use `short = '-'` to explicitly mark 'no short option'.",
    "default":      "Default value baked into generated code at schema-processing time. Mutually exclusive with `required = mandatory`. May be a variant-keyed block:\n```\ndefault = { ifdef LINUX_BUILD : \"/var/log/app.log\", default : \"./app.log\" }\n```",
    "required":     "Repetition/presence policy. `required = optional` (default) or `required = mandatory` (parser exits with error if missing).",
    "visible":      "Help visibility. `visible = all` (default, shown in `--help`), `visible = detail` (shown only in `--help-detail`), `visible = never` (never shown, but still parsed).",
    "multiple":     "Allows the option to be specified more than once, storing values in a static array.\n```\nmultiple = 8        /* up to 8 occurrences */\nmultiple = 1..32    /* 1 to 32 occurrences */\n```",
    "alias":        "Abbreviated alternative long-form name on the CLI.\n```\nalias = \"pp\"\n```",
    "depends-on":   "Names another option that must also be present whenever this option is set. `depends-on = optname`.",
    "conflicts":    "Names one option (or a list) that cannot be set at the same time as this option.\n```\nconflicts = other-opt\nconflicts = [ opt-a, opt-b ]\n```",
    "allowed":      "Conditional set of valid values. Arms use `ifdef`, `ifkey`, or `default`.\n```\nallowed {\n  ifdef FULL : (a, b, c, d)\n  default    : (a, b)\n}\n```",
    "sensitive":    "When `true`, the value is replaced with `***` in `cmdline_dump()` and passed as NULL to any logging callback. Use for tokens, passwords, keys.",
    "deprecated":   "Triggers a stderr warning when used. Value is the message shown to the user.",
    "display-unit": "Override the unit label shown in `--help` for quantity types (e.g. show `ms` instead of the stored `ns`).",
    "on-error":     "**Validation-failure policy (v2).** `exit` (default) makes the generated parser report the error and return non-zero; `warn` reports a warning, keeps the option's default (or clamps a numeric to the nearest bound), and continues. A project-wide default can be set with `meta { on-error = ... }`.",
    "units":        "**Accepted unit suffixes (v2).** Restricts a quantity type to a subset of its units, e.g. `duration units [us, ms, s]`. Disallowed units are rejected at parse time, the allowed set is shown in `--help`, and a default whose unit is not listed is rejected at generate time.",
    "unique":       "When `true`, reject duplicate values in a repeatable option (`multiple = N`). The generated parser checks for repeated identical arguments.",
    "help":         "Short one-line help text shown in `--help` output.",
    "details":      "Extended description shown in `--help-detail` and written to `cmdline.md`.",
    "note":         "Short note written only to `cmdline.md` (not shown in any `--help` variant).",
    "example":      "Usage example written to `cmdline.md`. May appear multiple times in the schema.",
    "since":        "Schema version string when this option was introduced. Written to `cmdline.md`.",

    // Meta keys
    "app":         "Application name (used in help headers and as the default binary name).",
    "brief":       "One-line description of the application or subcommand.",
    "version":     "Version string embedded in `--version` output.",
    "author":      "Author attribution written to generated documentation.",
    "prefix":      "C identifier prefix for all generated symbols (e.g. `prefix = \"nm\"` → `nm_cmdline_parse()`).",
    "output":      "Base name for generated files (e.g. `output = \"cmdline\"` → `cmdline.h`, `cmdline.c`, `cmdline.md`).",
    "doc-title":   "Heading used for the `cmdline.md` chapter. Defaults to the `app` name.",
    "description": "Multi-line description of the application or section written to `cmdline.md`.",
    "i18n":        "Directory of `.po` translation files. Enables pluggable help-text localisation.",

    // Qualifier values
    "optional":  "`required = optional` — option may be omitted (default behaviour).",
    "mandatory": "`required = mandatory` — option must appear on the CLI; the parser exits with an error if it is missing. Also used as a bare keyword inside a `group { }` to enforce exactly-one-of semantics.",
    "all":       "`visible = all` — option is shown in the standard `--help` output (default).",
    "detail":    "`visible = detail` — option is shown only in `--help-detail` (advanced / tuning knobs).",
    "never":     "`visible = never` — option never appears in any help output, but still parses normally. Use for internal or test-only flags.",

    // Type names
    "string":    "Fixed-buffer text field. `string` defaults to a 256-byte buffer; override with `string(length=N)`.",
    "path":      "Filesystem path stored in a fixed buffer. Same shape as `string`.",
    "file":      "Filesystem path that must refer to an existing regular file. Same storage as `path`.",
    "dir":       "Filesystem path that must refer to an existing directory. Same storage as `path`.",
    "bool":      "Boolean. Accepts `true`/`false`/`yes`/`no`/`on`/`off`/`1`/`0`. Supports `--no-foo` negation.",
    "flag":      "No-argument toggle. Presence on the CLI means `true`; absence means `false`.",

    // Quantity types
    "duration":  "Unit-aware time type. Stores `{ value, unit }` pair; no parse-time conversion. Generator emits `cf_duration_to_ns()` helper. Input accepts any compatible suffix: `ns`, `us`, `ms`, `s`, `m`, `h`, `d`.",
    "bytes":     "Unit-aware byte-count type. Stores `{ value, unit }` pair. Input: `B`, `KB`, `KiB`, `MB`, `MiB`, `GB`, `GiB`, `TB`, `TiB`.",
    "frequency": "Unit-aware frequency type. Stores `{ value, unit }` pair. Input: `Hz`, `kHz`, `MHz`, `GHz`.",
    "ratio":     "Ratio / percentage type. Stores `{ value, unit }` pair. Input: bare fraction (0.0–1.0) or `%` (0–100).",

    // Numeric types
    "uint8":  "Unsigned 8-bit integer (0–255).",
    "uint16": "Unsigned 16-bit integer (0–65535).",
    "uint32": "Unsigned 32-bit integer.",
    "uint64": "Unsigned 64-bit integer.",
    "sint8":  "Signed 8-bit integer (–128–127).",
    "sint16": "Signed 16-bit integer.",
    "sint32": "Signed 32-bit integer.",
    "sint64": "Signed 64-bit integer.",
    "float":  "32-bit IEEE-754 floating-point.",
    "double": "64-bit IEEE-754 floating-point.",

    // Directive clauses
    "as":      "Mandatory alias clause on `@import`. Alias is a C identifier and forms the dotted CLI prefix.",
    "dynamic": "Reserved keyword (v2). Will enable heap-backed dynamic storage. Rejected in v1.",
    "true":    "Boolean literal `true`.",
    "false":   "Boolean literal `false`.",
};
