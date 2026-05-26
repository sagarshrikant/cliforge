# cliforge Schema Specification — v1 (draft 0.3)

**Status:** Draft 0.3 — supersedes 0.2  
**Date:** 2026-05-21  
**Audience:** Implementers of the `cliforge` code generator, and authors of `.cf` schema files.

**Changes from 0.2**
- Three-tier condition system replaces `when`: `ifdef`/`ifndef` (CFLAGS/CPPFLAGS), `ifkey` (--key-words), `if` (runtime, inside `allowed`)
- `enum`/`struct` keywords removed; replaced by `()` choice type and `{}` compound type syntax
- New option qualifiers: `visible`, `multiple`, `alias`, `depends-on`, `conflicts`, `allowed`, `details`, `note`, `example`, `since`, `display-unit`
- `group` simplified: no `kind`, no `if`/`requires` fields; dependency between options uses `depends-on` on the option directly
- Documentation generation: `description`, `details`, `note`, `example` fields feed a generated `.md` chapter; Doxygen comments no longer copied into `cmdline.h`
- Build-system integration via `CFLAGS`/`CPPFLAGS` environment variables and `--key-words` flag
- `required = optional | mandatory` replaces `required = true | false`
- `short = '-'` as canonical "no short option" marker

---

## Table of contents

1. Introduction
2. What cliforge provides beyond gengetopt
3. Mental model
4. File conventions
5. Lexical structure
6. Top-level grammar
7. Type system
8. Unit-aware quantity types and storage
9. Compound types and sub-options
10. Options: qualifiers and behavior
11. Sections and groups
12. Imports and namespacing
13. Three-tier condition system
14. Build system integration
15. Documentation generation
16. Validation primitives
17. Positional arguments
18. Subcommands
19. Response files
20. Internationalization
21. Sensitive options
22. Generated code contract
23. CLI surface conventions
24. Reserved keywords
25. Schema versioning policy
26. v1 deliverables vs v2 roadmap
27. Worked examples

---

## 1. Introduction

`cliforge` is a code generator that transforms a declarative schema (`.cf` file) into portable C source (`cmdline.c` + `cmdline.h`) plus a generated Markdown reference chapter (`cmdline.md`). It targets the same problem as `gengetopt(1)` but extends the model substantially for modern multi-library C projects.

The generator itself is a single self-contained C executable with no third-party runtime dependencies. The C it emits compiles cleanly under C89, C99, and C11 and satisfies MISRA-C and Linux kernel coding style. The same generated headers are consumable from C++ and Rust (Rust bindings deferred to v2).

**Generated code is passive** — it exposes only parse APIs and helpers. The generator never imposes init/lifecycle hooks.

---

## 2. What cliforge provides beyond gengetopt

This section is for teams evaluating the migration from `gengetopt`. All gengetopt concepts (`required`, `optional`, `flag`, `short`, `default`, `multiple`, `hidden`, `group`, `mode`) have direct equivalents in cliforge; the schema language is different but the mental model maps cleanly.

### 2.1 Multi-library composition

gengetopt produces a single flat parser for a single binary. cliforge models the entire option surface of an application plus all libraries it links — static, shared, or `dlopen`'d — under one `--help`, with each library's options namespaced under an alias (`--arith.epsilon`, `--net.port`). Each library compiles its own `.cf` independently; the application imports them for help and namespacing only.

### 2.2 Compound (record) types

Options may carry multiple named sub-fields, parsed from a comma-separated `key=value` string on the CLI:

```
--filter name=blur,strength=5,enabled=true
--worker name=RENDER_1,policy=POLICY_ROUND,level=80,budget=256KiB
```

The generated struct stores each occurrence as a typed record. Static array of N records; heap-backed in v2.

### 2.3 Unit-aware quantity types

`duration`, `bytes`, `frequency`, and `ratio` types store a `{value, unit}` pair. The user may write `10ms`, `64KiB`, or `1.5MHz`; cliforge stores the value and unit as written and emits conversion helpers (`cc_duration_to_ns()`, etc.). No parse-time conversion occurs; the application converts on demand.

### 2.4 Three-tier conditional system

Conditions are first-class and clearly separated by stage:

| Keyword | Stage | Source |
|---------|-------|--------|
| `ifdef` / `ifndef` | Code-generation time | `-D` flags in `CFLAGS` / `CPPFLAGS` |
| `ifkey` | Code-generation time | `--key-words=` list |
| `if` | Runtime (generated C validation) | Option values on the actual CLI |

Build-time conditions gate which options exist in the generated struct. Runtime conditions (`allowed` blocks) gate which values are valid for an option given other options already parsed.

### 2.5 Conditional allowed values (`allowed` blocks)

An option's valid values may vary based on other options or build flags:

```
option output-fmt {
  type = (json, csv, binary)
  allowed {
    ifdef ARCH_ARM64                               : (json, csv, binary)
    if    !batch-mode                              : (json, csv)
    if    batch-mode && log-dest == FILE           : (csv)
  }
}
```

The generated C struct always carries the superset type; the parser validates at runtime.

### 2.6 Section-local type declarations

Named choice and compound types may be declared inside a `section` block, directly above the options that use them. This keeps valid values visible at the point of use without requiring the reader to scroll.

### 2.7 Generated documentation chapter

cliforge emits a `cmdline.md` Markdown file in addition to `cmdline.h`/`cmdline.c`. The `.md` file is a structured reference chapter suitable for inclusion in Sphinx, Doxygen, or any Markdown pipeline. It is built from `description`, `help`, `details`, `note`, and `example` fields in the schema — not from Doxygen comments copied into a C header.

### 2.8 MISRA-C and Linux kernel coding style

Generated code targets MISRA-C:2012 and Linux kernel style: no VLAs, no dynamic allocation by default, no `goto` across initialisers, `/* */` comments only (no `//` in generated C89 output), explicit casts, no implicit fallthrough.

### 2.9 Additional features over gengetopt

- `alias` — abbreviated long-form option name (`--bm` as alias for `--batch-mode`)
- `depends-on` — declare that option A requires option B to also be set
- `conflicts` — declare that option A cannot be used with option B
- `visible = all | detail | never` — three-tier visibility (normal help, detail help, never)
- `sensitive` — mask value in dumps and audit logs
- Scoped `--help[=NAME]` — target help to a section, group, or option by name
- Response files in both `-C file` and `@file` forms
- Subcommands (git-style)
- Man-page generation
- i18n hook (pluggable translator + `.pot` extractor)
- LSP server for IDE support, published as a VS Code extension

---

## 3. Mental model

Think of cliforge as a small compiler:

- The `.cf` schema is the **source code**.
- The generated `cmdline.c`, `cmdline.h`, and `cmdline.md` are the **outputs**.
- `@import` is to a schema what `#include` is to a C translation unit — it brings another module's option surface into scope for `--help` and namespacing, but each imported `.cf` still compiles independently.

Build-time conditions (`ifdef`/`ifndef`/`ifkey`) behave like `#if`/`#ifdef` in C: the generator evaluates them when it runs and includes or excludes schema elements accordingly. Runtime conditions (`if` in `allowed`) become C validation logic in the generated parser, evaluated when the end user runs the compiled program.

---

## 4. File conventions

- **Extension:** `.cf` (cliforge schema).
- **Encoding:** UTF-8. No BOM.
- **Line endings:** LF or CRLF (both accepted; the generator normalises internally).
- **Indentation:** 2 or 4 spaces recommended. Tabs accepted but discouraged. The grammar is indentation-insensitive.
- **Whitespace:** Significant only as a token separator.

Every schema file **must** begin with a version-declaration directive (see §6.1).

---

## 5. Lexical structure

### 5.1 Identifiers

```
identifier  := letter (letter | digit | '_' | '-')*
letter      := 'A'..'Z' | 'a'..'z' | '_'
digit       := '0'..'9'
```

Option names may use hyphens (`--keep-alive`); the generator translates hyphens to underscores in generated C symbols (`keep_alive`).

### 5.2 Numeric literals

```
integer    := ('+' | '-')? (decimal | hex | octal | binary)
decimal    := digit+
hex        := '0x' hexdigit+
octal      := '0o' ('0'..'7')+
binary     := '0b' ('0' | '1')+
float      := ('+' | '-')? digit+ '.' digit+ (('e'|'E') ('+'|'-')? digit+)?
```

Underscores are permitted as digit separators (`1_000_000`, `0xFFFF_FFFF`).

### 5.3 String literals

```
string     := '"' character* '"'
```

Escape sequences: `\n`, `\t`, `\r`, `\\`, `\"`, `\0`, `\xHH`, `\uHHHH`, and `\` followed by a newline (line continuation). Adjacent string literals separated only by whitespace concatenate (`"foo" "bar"` → `"foobar"`).

### 5.4 Quantity literals

A numeric literal immediately followed by a unit suffix (no space):

```
quantity   := (integer | float) suffix
suffix     := time_unit | byte_unit | freq_unit | ratio_unit
time_unit  := 'ns' | 'us' | 'µs' | 'ms' | 's' | 'm' | 'h' | 'd'
byte_unit  := 'B' | 'KB' | 'KiB' | 'MB' | 'MiB' | 'GB' | 'GiB' | 'TB' | 'TiB'
freq_unit  := 'Hz' | 'kHz' | 'MHz' | 'GHz'
ratio_unit := '%'
```

Examples: `10ms`, `64KiB`, `2.5MHz`, `12%`. The unit must belong to the same group as the option's declared type (§7.4).

### 5.5 Comments

```
// line comment            (discarded)
/* block comment */        (discarded)
```

Doc comments have been removed from the schema language. Documentation is provided through first-class fields (`help`, `details`, `note`, `example`) — see §15.

### 5.6 Operators

```
{ } [ ] ( ) = , : . @ ;
&& || ! == != > < >= <=
..
```

Semicolons are optional terminators. Trailing commas are permitted in any comma-separated list. `==` and `!=` are used in `if`/`ifdef`/`ifkey` condition expressions (§13). `>`, `<`, `>=`, `<=` are used in numeric `if` comparisons.

---

## 6. Top-level grammar

### 6.1 Schema version directive

The first non-comment, non-whitespace tokens of every schema **must** be:

```
@schema cliforge v1
```

### 6.2 Top-level forms

```
top_level   := schema_directive
             | import_directive
             | meta_block
             | type_decl          // name = (...) or name = { ... }
             | section_block
             | option_decl
             | group_decl
             | positional_block
             | subcommand_block
             | i18n_block
```

### 6.3 The `meta` block

```
meta {
  app         = "calctool"
  brief       = "Filter-capable expression evaluator"
  version     = "0.1.0"
  author      = "Your Name"
  prefix      = "cc"          // C symbol prefix; default: app name sanitised
  output      = "cmdline"     // base filename for generated cmdline.c/h/md
  i18n        = "po/"         // optional: i18n .po directory
  doc-title   = "calctool Command-line Reference"   // .md chapter title
  description = "\
    calctool evaluates mathematical expressions from the command line. \
    It supports filters loaded as shared objects for extended operators. \
  "
}
```

Required: `app`. All others optional. For a **library** schema set `kind = library`:

```
meta {
  app    = "arith"
  kind   = library
  prefix = "arith"
  output = "cmdline_arith"
}
```

`doc-title` defaults to `"<app> Command-line Reference"`. `description` seeds the introductory paragraph of the generated `cmdline.md` chapter.

---

## 7. Type system

### 7.1 Primitive types

| Schema type | C mapping | Notes |
|-------------|-----------|-------|
| `int8`      | `int8_t`  | Alias: `sint8` |
| `int16`     | `int16_t` | Alias: `sint16` |
| `int32`     | `int32_t` | Alias: `sint32`; `int` also accepted (gengetopt compat) |
| `int64`     | `int64_t` | Alias: `sint64`; `long` also accepted |
| `uint8`     | `uint8_t` |
| `uint16`    | `uint16_t` |
| `uint32`    | `uint32_t` |
| `uint64`    | `uint64_t` |
| `float`     | `float`   | IEEE 754 single |
| `double`    | `double`  | IEEE 754 double |
| `bool`      | `bool` (C99+) / `uint8_t` (C89) | Accepts `true`/`false`/`yes`/`no`/`on`/`off`/`1`/`0` |
| `flag`      | `bool`    | No-argument toggle; presence on CLI means `true` |
| `string`    | `char[N]` static buffer | `string` or `string(length=N)`; default N=256 |
| `file`      | `char[N]` static buffer | Path to a regular file; validated not a directory |
| `dir`       | `char[N]` static buffer | Path to a directory |
| `path`      | `char[N]` static buffer | Either file or directory (no validation) |

All storage is in caller-provided fixed buffers (malloc-free by default).

**Portability.** Generated code includes only: `<stdint.h>`, `<stddef.h>`, `<string.h>`, optionally `<stdbool.h>` (C99+, suppressed under `--std=c89`), optionally `<stdio.h>`.

### 7.2 Choice types (inline and named)

A choice type is declared with parentheses. Values may use any case convention.

**Inline** — used in one option only:

```
option mode {
  type    = (integer, real, complex)
  default = real
}

option policy {
  type    = (POLICY_ROUND, POLICY_IDLE)
  default = POLICY_ROUND
}
```

**Named** — declared at top level or inside a `section`, referenced by name:

```
// Top-level declaration
verbosity = (quiet, normal, verbose, trace)

// Section-level declaration (visible right next to where it is used)
section "Workers" {
  policy-class = (POLICY_ROUND, POLICY_FIFO, POLICY_IDLE)

  option worker {
    type = {
      policy : policy-class = POLICY_ROUND
      level  : uint8        = 1
    }
    multiple = 4
  }
}
```

**Generated C.** A choice type `verbosity` with prefix `cc` becomes:

```c
enum cc_verbosity { CC_VERBOSITY_QUIET, CC_VERBOSITY_NORMAL,
                    CC_VERBOSITY_VERBOSE, CC_VERBOSITY_TRACE };
```

Plus `cc_verbosity_from_str()` and `cc_verbosity_to_str()` helpers.

**Section-scoped types** have names scoped to the generated C file (not to a C scope or namespace). Their C names include the section title: `cc_workers_policy_class`. They are visible in the schema wherever the name is written; the section boundary is for documentation and readability only, not for symbol access.

### 7.3 Compound types (inline and named)

A compound type is declared with braces. Each field has a name, type, optional default, and optional range constraint.

**Inline** — one-off records:

```
option filter {
  type = {
    name     : string(length=32)  = "anon"
    strength : uint8 in 0..255    = 100
    enabled  : bool               = true
  }
  multiple = 16
}
```

**Named** — reusable across multiple options:

```
// Named compound type declared at top level
filter-spec = {
  name     : string(length=32)  = "anon"
  strength : uint8 in 0..255    = 100
  enabled  : bool               = true
}

option filter       { type = filter-spec; multiple = 16 }
option debug-filter { type = filter-spec; visible = detail }
```

**Field grammar:**

```
field := identifier ':' type_expr ('in' range)? ('=' value_expr)?
```

Fields may carry quantity types, choice types, or primitive types. Nested compound types are not supported in v1.

### 7.4 Quantity types

Unit-aware types. No parse-time conversion occurs; storage is `{value, unit}`.

```
option deadline { type = duration;  default = 250ms }
option cache    { type = bytes;     default = 4MiB  }
option clock    { type = frequency; default = 1.5MHz }
option duty     { type = ratio;     default = 25%   }
```

See §8 for full detail on quantity types and conversion helpers.

---

## 8. Unit-aware quantity types and storage

### 8.1 Unit groups

| Group       | Accepted suffixes |
|-------------|-------------------|
| `duration`  | `ns`, `us` (`µs`), `ms`, `s`, `m`, `h`, `d` |
| `bytes`     | `B`, `KB`, `KiB`, `MB`, `MiB`, `GB`, `GiB`, `TB`, `TiB` |
| `frequency` | `Hz`, `kHz`, `MHz`, `GHz` |
| `ratio`     | bare number (fraction 0..1), `%` |

For `bytes`, decimal (`KB` = 10³) and binary (`KiB` = 2¹⁰) units are distinct.

### 8.2 Generated storage

```c
enum cc_duration_unit {
    CC_DURATION_NS, CC_DURATION_US, CC_DURATION_MS,
    CC_DURATION_S,  CC_DURATION_M,  CC_DURATION_H, CC_DURATION_D
};

struct cc_duration {
    uint64_t              value;  /* magnitude as written */
    enum cc_duration_unit unit;   /* unit as written      */
};
```

### 8.3 Conversion helpers

```c
uint64_t cc_duration_to_ns(const struct cc_duration *q);
uint64_t cc_duration_to_ms(const struct cc_duration *q);
double   cc_duration_to_s_d(const struct cc_duration *q);  /* lossless float */
/* one helper per unit, plus _d (double) variants */
```

Out-of-range conversions return 0 and set `errno = ERANGE` (when `<errno.h>` is available).

### 8.4 Defaults and range constraints

```
option deadline {
  type         = duration in 1ms..10s
  default      = 250ms
  display-unit = ms             // shown in --help as "(default: 250 ms)"
}
```

Defaults are stored as the user wrote them. Range bounds may mix units within the same group; the generator converts both to a common base at codegen time for the comparison only.

---

## 9. Compound types and sub-options

### 9.1 CLI syntax

Each occurrence is a single argv element with comma-separated `key=value` pairs:

```
--filter name=blur,strength=5,enabled=true
--worker name=RENDER_1,policy=POLICY_ROUND,level=80,budget=256KiB
```

Fields omitted on the CLI take their declared defaults. Unknown field names are a parse error.

### 9.2 Quoting

A value containing `,` or `=` must be quoted:

```
--threshold name="t,x",kind=warn
```

### 9.3 Generated storage

```c
struct cc_filter_spec {
    char    name[32];
    uint8_t strength;
    bool    enabled;
};

struct cc_args {
    struct cc_filter_spec filter[16];
    size_t                filter_count;
};
```

### 9.4 Tuple defaults

A compound option's default is specified as a named tuple:

```
option worker {
  type = {
    name   : worker-name  = ALL
    policy : policy-class = POLICY_ROUND
    level  : int8         = 1
    budget : bytes        = 512KiB
  }
  default  = (name=ALL, policy=POLICY_ROUND, level=1, budget=512KiB)
  multiple = 4
}
```

---

## 10. Options: qualifiers and behavior

### 10.1 Full qualifier reference

```
option <name> {
  // Type and CLI identity
  type          = <type_expr>
  short         = '<char>' | '-'     // '-' means no short form
  alias         = "<long-name>"      // abbreviated long form e.g. "batch"

  // Default and requirement
  default       = <value_expr>
  required      = optional | mandatory

  // Repetition
  multiple      = <N>                // static array of capacity N (v1)
  multiple      = <min>..<max>       // both min and max
  unique        = true | false       // reject duplicate values

  // Visibility in --help
  visible       = all | detail | never
  // 'all'    — shown in --help (default)
  // 'detail' — shown only under --help-detail
  // 'never'  — never shown in any help; still parses (internal/CI toggles)

  // Build-time inclusion (§13)
  ifdef   <expr>
  ifndef  <expr>
  ifkey   <expr>

  // Runtime conditional allowed values (§13.4)
  allowed { ... }

  // Inter-option relationships
  depends-on    = <option-name>      // this option requires that one also be set
  conflicts     = <option-name>      // cannot be used together; or [ list ]

  // Documentation (feeds cmdline.md — see §15)
  help          = "<one-liner>"      // shown in --help output
  details       = "<longer text>"    // shown in --help-detail AND cmdline.md
  note          = "<text>"           // cmdline.md only (not in any --help)
  example       = "<CLI snippet>"    // cmdline.md only; repeat for multiple
  since         = "<version>"        // API stability note in cmdline.md

  // Display hints
  display-unit  = <unit>             // unit shown in --help (quantity types only)

  // Security
  sensitive     = true | false       // mask value in dump() and audit logs

  // Lifecycle
  deprecated    = "<message>"        // warning printed when used
}
```

### 10.2 `short` and `alias`

`short` is a single character (`short = 'v'`). Use `short = '-'` to explicitly declare no short form. Omitting `short` is also valid and implies no short form.

`alias` provides an abbreviated long-form name. The user may type `--batch` as a shorthand for `--batch-mode`:

```
option batch-mode {
  type  = (on, off)
  alias = "batch"
}
```

### 10.3 `required` and `default`

These are mutually exclusive. `required = mandatory` with no `default` causes a parse error if the option is absent. `required = optional` with a `default` bakes the default into the generated constant initialiser.

### 10.4 `multiple`

```
multiple = 4         // static array of capacity 4; user may supply 0..4
multiple = 1..4      // min 1, max 4; fewer than 1 is a parse error
multiple = 1..       // min 1, no upper limit (up to compile-time MAX constant)
```

For repeatable options: storage is `T values[N]; size_t count;`. For compound types: storage is an array of the compound struct.

### 10.5 `visible`

Three tiers, replacing the former `hidden` and `strict_hidden` booleans:

- `visible = all` — default. Shows in `--help`.
- `visible = detail` — omitted from `--help`; shown by `--help-detail`. Use for power-user/debug options.
- `visible = never` — never appears in any help output. Still parses. Use for internal or CI-only toggles.

### 10.6 `depends-on` and `conflicts`

```
option tls-cert {
  type       = file
  required   = optional
  depends-on = tls        // tls-cert is only valid (and required) when --tls is set
}

option csv-out {
  type      = flag
  conflicts = json-out    // cannot specify both --csv-out and --json-out
}

option write-config {
  type      = file
  conflicts = [ read-only, dry-run ]   // list form
}
```

The generated parser enforces both constraints as a post-parse validation step.

### 10.7 `allowed` blocks

See §13.4 for the full condition syntax.

### 10.8 `deprecated`

```
option debug-log {
  type       = bool
  default    = false
  deprecated = "use --verbose 4 instead (since 0.3.0)"
}
```

The generated parser prints a deprecation warning to stderr (or the write callback) before continuing.

### 10.9 `sensitive`

When `sensitive = true`, `cmdline_dump()` prints `***` for the value. The optional logging callback (§22.5) receives `NULL` for sensitive option values.

---

## 11. Sections and groups

### 11.1 Sections

```
section "General" {
  description = "Core application options."

  // Named types may be declared inside sections for proximity to their use
  verbosity = (quiet, normal, verbose, trace)

  option verbose {
    type    = verbosity
    short   = 'v'
    default = normal
    help    = "Verbosity level."
  }
}
```

Sections affect `--help` rendering and `cmdline.md` structure only. They do not introduce a C namespace. Type declarations inside sections are visible by name throughout the schema.

Conditional sections:

```
section "Workers" {
  ifdef DESKTOP_BUILD || UNIX_BUILD
  description = "Worker scheduling options."

  option mlock { type = bool; default = false; help = "Lock memory pages." }
}
```

### 11.2 Groups

A `group` declares a set of options that are mutually exclusive.

```
// Zero or one of these may be active (optional exclusive)
group output-mode {
  options = [ json-out, csv-out, binary-out ]
}

// Exactly one must be active (mandatory exclusive)
group output-format {
  options   = [ json, xml, plain ]
  mandatory
}
```

Groups generate a post-parse validation block in the generated C. They also cause `--help` to visually bracket the options.

**Group does NOT replace `depends-on`.** For "if A is set, B must also be set" relationships, use `depends-on` directly on the option (§10.6).

```c
/* group 'output-format': exactly one must be active */
{
    int _g = args->json_given + args->xml_given + args->plain_given;
    if (_g != 1) {
        fprintf(stderr, "%s: exactly one of --json, --xml, --plain required\n", argv[0]);
        return CC_PARSE_ERR_GROUP;
    }
}
```

---

## 12. Imports and namespacing

### 12.1 Syntax

```
@import "path/to/arith.cf"  as arith
@import "path/to/trig.cf"   as trig   ifkey have-trig
@import "path/to/dyn.cf"    as dyn    ifdef DESKTOP_BUILD
```

The `as <alias>` clause is **mandatory**. The `ifkey`/`ifdef`/`ifndef` clause is optional; it gates whether the import is processed at all.

### 12.2 What `@import` does

- Tells the importing schema that some CLI options are owned by the imported library, and should appear under `--<alias>.<option>` in `--help`.
- Does **not** copy or re-validate options into the application's parser.
- Does **not** run cliforge on the imported file; each library compiles its own `.cf` separately.

### 12.3 Runtime model

The application's generated parser routes `--<alias>.<rest>` tokens into a per-alias argv slice. The application hands each slice to the library's own generated parser:

```c
struct cc_args app;
cc_cmdline_parse(argc, argv, &app);

struct arith_args a;
arith_cmdline_parse_slice(app.arith_argv, app.arith_argc, &a);
```

### 12.4 `owner = application` escape hatch

```
option config-path {
  type  = path
  owner = application
  help  = "Path to lib config; app reads it on the lib's behalf"
}
```

The lib's parser ignores this option; the importing app routes it into its own struct.

### 12.5 dlopen modules — not `@import`ed

Runtime-discovered modules cannot be statically `@import`ed. The host declares a repeatable compound option (e.g. `--filter name=…,strength=…`). Each module author writes their own `.cf` using `lib/filter_api.cf` as a template. See §27.

### 12.6 Missing imports

If the named file doesn't exist and the import is not guarded by a condition, the generator emits a hard error.

---

## 13. Three-tier condition system

Conditions in cliforge are always explicit about *when* they are evaluated. Three tiers map to three stages of a build system.

### 13.1 `ifdef` / `ifndef` — build-time (CFLAGS / CPPFLAGS)

Evaluated when cliforge runs. cliforge reads the `CFLAGS` and `CPPFLAGS` environment variables and extracts any `-D SYMBOL` or `-DSYMBOL=VALUE` flags from them.

```
option unix-sock {
  type    = flag
  default = off
  ifdef   DESKTOP_BUILD || UNIX_BUILD
  help    = "Use UNIX domain socket transport."
}

option windows-pipe {
  type    = flag
  default = off
  ifndef  DESKTOP_BUILD
  help    = "Use Windows named pipe transport."
}
```

**Value tests:**

```
ifdef ARCH == arm64                 // true if -DARCH=arm64 in CFLAGS
ifndef PLATFORM == embedded         // true if PLATFORM is not defined or not "embedded"
```

**Boolean expressions:** `&&`, `||`, `!`, `()` grouping.

**On `@import`:**

```
@import "lib/trig.cf" as trig  ifdef HAVE_TRIG
```

**On `section`:** write `ifdef EXPR` as a line inside the section body before any options.

### 13.2 `ifkey` — schema-key conditions (--key-words)

Evaluated when cliforge runs. Keys are supplied via the `--key-words` CLI flag:

```sh
cliforge --key-words="have-trig,have-stats,target=embedded" calctool.cf
```

Keys are user-defined schema-processing tags that may or may not correspond to compiler `-D` flags. Useful for cliforge-specific conditional features that are not compiler flags.

```
@import "lib/trig.cf" as trig   ifkey have-trig

option hw-init {
  type  = bool
  ifkey target == embedded
  help  = "Run hardware-specific initialisation."
}
```

**Syntax is identical to `ifdef`/`ifndef`** with `ifkey` / `ifnkey` as the keyword pair.

### 13.3 `if` — runtime conditions (generated C validation)

Used exclusively inside `allowed` blocks (§13.4) and `depends-on` expressions. Evaluated in the generated C parser when the end user runs the program.

```
if optname                       // option was supplied on CLI
if !optname                      // option was not supplied
if optname == VALUE              // option has this value
if optname != VALUE
if optname > N                   // numeric comparison
if optname >= N
```

Logical: `&&`, `||`, `!`, `()` grouping.

### 13.4 `allowed` blocks

An `allowed` block constrains which values are accepted for a choice-type option, conditionally:

```
option output-fmt {
  type    = (json, csv, binary)       // C struct always has the superset type
  allowed {
    ifdef ARCH_ARM64                               : (json, csv, binary)
    if    !batch-mode                              : (json, csv)
    if    batch-mode && log-dest == FILE           : (csv)
  }
  default  = json
  required = optional
  help     = "Output format type."
}
```

Each line in the `allowed` block is: `<condition> : <allowed-values>`. Conditions are evaluated in order; the first matching line wins. If no condition matches, all declared type values are allowed (i.e. an unmatched `allowed` block is an error if exhaustive; the generator warns when the block may be non-exhaustive).

`ifdef`/`ifkey` lines in `allowed` become `#if` guards in the generated C. `if` lines become runtime validation checks.

### 13.5 Variant-keyed defaults

For options whose default value varies by build:

```
option logfile {
  type    = file
  default = {
    ifdef DESKTOP_BUILD  : "/var/log/app.log"
    ifdef RTOS_BUILD     : "/fs/log/app.log"
    default              : "./app.log"
  }
}
```

The `default:` arm is required. At codegen time exactly one arm is selected; first match wins (with a generator warning on ties).

---

## 14. Build system integration

### 14.1 How cliforge receives build-time information

cliforge **does not** take `-D` flags on its command line. Instead it reads the build system's own environment:

```sh
# cliforge reads CFLAGS and CPPFLAGS from the environment automatically
export CFLAGS="-DDESKTOP_BUILD -DARCH=arm64 -O2"
cliforge --key-words="have-trig,have-stats" calctool.cf -o build/
```

This keeps cliforge in sync with the compiler: the same `-D` flags the compiler sees, cliforge sees. No separate `-D` flag list to maintain.

### 14.2 `--key-words` flag

Schema-specific processing keys that are not compiler flags:

```sh
cliforge --key-words="have-trig,have-stats,target=embedded" calctool.cf
```

Key-value form: `key=value`. Bare form: `key` (equivalent to `key=true`). Multiple keys: comma-separated or repeated `--key-words` flags.

### 14.3 CMake integration

The `cliforge.cmake` module ships with cliforge. It provides `cliforge_generate()`:

```cmake
find_package(cliforge REQUIRED)

cliforge_generate(
  SCHEMA      ${CMAKE_CURRENT_SOURCE_DIR}/calctool.cf
  KEY_WORDS   have-trig have-stats target=embedded
  OUTDIR      ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS     lib/arith.cf lib/trig.cf
)
```

The module:
1. Forwards the current CMake `CMAKE_C_FLAGS` and related variables as `CFLAGS`/`CPPFLAGS` to cliforge's environment.
2. Passes `KEY_WORDS` as `--key-words`.
3. Lists all `.cf` files in `DEPENDS` so CMake re-runs cliforge when schemas change.

### 14.4 Makefile integration

```makefile
CFLAGS := -DDESKTOP_BUILD -DARCH=arm64

cmdline.c cmdline.h cmdline.md: calctool.cf lib/arith.cf
	cliforge --key-words="have-trig" calctool.cf -o $(OUTDIR)/
```

---

## 15. Documentation generation

### 15.1 Generated outputs

For every schema, cliforge produces three outputs:

- `cmdline.h` — C declarations with minimal MISRA-style `/* */` comments. No Doxygen. No doc strings.
- `cmdline.c` — C implementation. Minimal inline comments.
- `cmdline.md` — Markdown reference chapter built from schema documentation fields.

### 15.2 Schema documentation fields

| Field | Applies to | Goes to | Content |
|-------|-----------|---------|---------|
| `description` | `meta`, `section` | `.md` chapter intro / section preamble | Multi-line, `\` continuation |
| `help` | `option` | `--help` output AND `.md` | One-liner (< 80 chars) |
| `details` | `option` | `--help-detail` output AND `.md` | Longer explanation |
| `note` | `option` | `.md` only | Implementation notes, conditional logic explanation |
| `example` | `option` | `.md` only | Example CLI invocation; may repeat |
| `since` | `option`, `meta` | `.md` only | Version since the option exists |
| `doc-title` | `meta` | `.md` chapter heading | Overrides default chapter title |

### 15.3 Example

```
option deadline {
  type         = duration in 1ms..10s
  default      = 250ms
  display-unit = ms
  required     = optional
  help         = "Soft timeout per evaluation."
  details      = "\
    If an evaluation exceeds this deadline the parser returns \
    CC_PARSE_WARN_TIMEOUT and continues. Set 0 to disable. \
  "
  note         = "Has no effect when --mode=batch is active."
  example      = "calctool --deadline 500ms eval '1/3'"
  since        = "0.2.0"
}
```

The generated `cmdline.md` section for this option includes: the `help` text as a brief description, `details` as a paragraph, `note` in a callout block, and `example` as a code block.

### 15.4 `allowed` block documentation

The generator automatically documents conditional allowed values in `cmdline.md`:

```markdown
**output-fmt** `(json | csv | binary)`

Output format type.

Allowed values depend on context:
- When `ARCH_ARM64` is defined: `json`, `csv`, `binary`
- When `--batch-mode` is not set: `json`, `csv`
- When `--batch-mode` is set and `--log-dest` is `FILE`: `csv`
```

---

## 16. Validation primitives

| Constraint | Applies to | Example |
|------------|-----------|---------|
| `in` | numeric, quantity | `in 1..99`, `in 1ms..10s` |
| `min` | numeric, quantity, string length | `min = 8` |
| `max` | numeric, quantity, string length | `max = 64` |
| `matches` | string, path, file, dir | `matches "*.so"` |
| `required = mandatory` | option | parse error if absent |
| `unique` | repeatable options | reject duplicate values |

`matches` accepts: `*`, `?`, `[abc]`, `[!abc]`, `[a-z]`. Anchored at both ends.

---

## 17. Positional arguments

### 17.1 Declaration

```
positional expressions {
  type     = string(length=1024)
  multiple = 32         // variadic; up to 32 expressions
  required = mandatory  // at least one required (use with multiple = 1..)
  help     = "Expressions to evaluate."
}

positional filter-name {
  type     = string(length=32)
  required = mandatory
  help     = "Filter name to benchmark."
}
```

### 17.2 CLI

Positionals come after `--` or after any non-`-` argv element:

```
calctool --precision 16 -- "2 + 2" "pi * 3"
calctool "2 + 2"
```

### 17.3 Generated storage

```c
struct cc_args {
    char   expressions[32][1024];
    size_t expressions_count;
};
```

---

## 18. Subcommands

### 18.1 Declaration

```
subcommand eval {
  brief       = "Evaluate an expression"
  description = "Parses and evaluates one or more mathematical expressions."

  option echo {
    type     = bool
    short    = 'e'
    default  = false
    help     = "Echo each expression before its result."
  }

  positional expressions {
    type     = string(length=1024)
    multiple = 32
    help     = "Expressions to evaluate."
  }
}
```

### 18.2 Dispatch

```
calctool eval --precision 20 -- "2 + 2"
calctool bench --iterations 5000 fin
```

### 18.3 Generated dispatch

```c
struct cc_args {
    enum cc_subcommand cmd;        /* CC_CMD_NONE, CC_CMD_EVAL, CC_CMD_BENCH */
    union {
        struct cc_eval_args  eval;
        struct cc_bench_args bench;
    };
};
```

---

## 19. Response files

Two forms accepted on the CLI:

```
calctool -C args.cfargs      # gengetopt-compatible form
calctool @args.cfargs        # GCC/clang style
```

Both splice the file's contents into argv. File format: one token per line; `#` comments; blank lines ignored; shell-style quoting and `\` escapes; nested references allowed (cycles detected and rejected).

---

## 20. Internationalization

Declaring `i18n` in `meta` activates cliforge's translation hook. Without it, nothing changes — zero overhead for tools that don't need it.

```
meta {
  i18n = "po/"   /* directory where .po translation files live */
}
```

### What "pluggable translator" means

Without `i18n`, cliforge emits help strings directly:

```c
/* generated WITHOUT i18n */
printf("Filter by project name.\n");
```

With `i18n`, every user-visible string is routed through a `TR()` macro:

```c
/* generated WITH i18n */
printf("%s\n", TR("Filter by project name."));
```

`TR()` is a **hook** — a socket you wire once in your application. You decide what plugs in:

```c
/* Option A: standard gettext (most common on Linux) */
#define TR(s)  gettext(s)

/* Option B: your own resolver */
#define TR(s)  my_translate(s)

/* Option C: no translation needed — zero cost */
#define TR(s)  (s)
```

All help text, error messages, and descriptions flow through the same hook. cliforge wires the socket; you choose the plug.

### What `.pot` extractor means

A `.pot` file (**Portable Object Template**) is the standard "shopping list" that translators fill in. It looks like:

```
msgid "Filter by project name."
msgstr ""

msgid "Output file path."
msgstr ""
```

`cliforge-i18n-extract` scans your `.cf` schema and **automatically generates this list** — collecting every `help =`, `brief =`, and other user-visible string — so translators get a complete, accurate template without you maintaining one by hand.

Translators fill in `msgstr` for each language (`calctool.de.po`, `calctool.fr.po`, …), and `gettext()` (or your resolver) picks them up at runtime.

### End-to-end flow

```
Your .cf schema
      │
      ▼
cliforge ──────────────────► cmdline.c / cmdline.h
                               (TR("...") hook wired in)
      │
      ▼
cliforge-i18n-extract ──────► calctool.pot
                               (all help strings collected)
      │
      ▼  (translators fill in .po files)
      │
      ▼
Runtime: TR("Filter by...") → gettext() → "Nach Projektname filtern."
```

### Developer quick-start (three steps)

**Step 1 — Declare in schema:**
```
meta {
  i18n = "po/"
}
```

**Step 2 — Wire the hook once in your app (e.g. `main.c`):**
```c
#include <libintl.h>
#include <locale.h>
#define TR(s) gettext(s)

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    bindtextdomain("calctool", "po/");
    textdomain("calctool");

    struct cc_args args;
    cc_cmdline_parse(argc, argv, &args);
    /* --help now prints in the user's system locale */
}
```

**Step 3 — Extract strings for translators:**
```sh
cliforge-i18n-extract calctool.cf -o po/calctool.pot
```

That's it. If no `.po` files exist yet, `TR()` falls back to the original English string — the tool works correctly from day one.

### Notes

- `i18n` is **opt-in** — omit it and the `TR()` macro is never emitted; no overhead.
- Generated code remains C89-compatible — `TR()` is a macro call, not a function pointer or vtable.
- Any resolver works: `gettext`, a custom lookup table, or a no-op identity `#define TR(s) (s)`.

---

## 21. Sensitive options

```
option api-token {
  type      = string(length=128)
  sensitive = true
  help      = "Service API token."
}
```

Effects: `cmdline_dump()` prints `***`; the logging callback receives `NULL`; `cmdline.md` flags the option as sensitive.

---

## 22. Generated code contract

### 22.1 File naming

Given `meta.output = "cmdline"` and `meta.prefix = "cc"`:

- `cmdline.h` — public interface
- `cmdline.c` — implementation
- `cmdline.md` — Markdown reference chapter

### 22.2 Public symbols

```c
struct cc_args {
    /* ... option fields ... */
    int         cliforge_error;
    const char *cliforge_error_msg;
};

int   cc_cmdline_parse(int argc, char **argv, struct cc_args *out);
int   cc_cmdline_parse_slice(char **slice, size_t n, struct cc_args *out);
void  cc_cmdline_free(struct cc_args *args);                /* no-op in static mode */
void  cc_cmdline_print_help(FILE *out, const char *target);
void  cc_cmdline_print_help_detail(FILE *out, const char *target);
void  cc_cmdline_print_version(FILE *out);
void  cc_cmdline_dump(FILE *out, const struct cc_args *args);

extern const char *const cc_cmdline_schema_version;
extern const char *const cc_cmdline_app_version;
```

### 22.3 Error model

`parse()` returns 0 on success, negative on error. `out->cliforge_error` holds a stable enum code; `cliforge_error_msg` points to a static string.

### 22.4 No-stdio mode

`--no-stdio` swaps `FILE*` for a write callback:

```c
typedef void (*cc_write_fn)(void *ctx, const char *buf, size_t len);
void cc_cmdline_print_help(cc_write_fn fn, void *ctx, const char *target);
```

### 22.5 Logging callback

```c
typedef void (*cc_log_fn)(void *ctx,
                           const char *option_name,
                           const char *value);  /* NULL if sensitive */
void cc_cmdline_set_logger(cc_log_fn fn, void *ctx);
```

### 22.6 Thread safety

`parse()` writes only to the caller's struct. No global mutable state. Multiple threads may call `parse()` concurrently with distinct struct instances. `print_help()` reads static const tables; safe to call concurrently.

### 22.7 Memory model

Default (static) mode: zero heap allocation. All storage in the caller's struct. `cmdline_free()` is a no-op.

Dynamic mode (v2): heap-backed string and repeatable storage.

### 22.8 Per-import argv slicing

```c
struct cc_args {
    char  **arith_argv;   /* points into original argv */
    size_t  arith_argc;
};
```

### 22.9 C89/C99/C11 toggles

`--std=c89|c99|c11`:

- `c89`: no `<stdbool.h>`, no `//` comments, no designated initializers.
- `c99` (default): `<stdbool.h>`, `<stdint.h>`, designated initializers.
- `c11`: adds `_Static_assert` for range/size invariants.

---

## 23. CLI surface conventions

### 23.1 Long options

```
--foo                 # bool: sets true
--no-foo              # bool: sets false (bool/flag types only)
--foo=value           # single value
--foo value           # single value
```

### 23.2 Short options

```
-f                    # bool toggle
-fvalue               # short option with attached value
-f value              # short option with detached value
-abc                  # combination of three bool/flag short options
```

### 23.3 Imported options

```
--arith.epsilon 1e-12
--trig.units degrees
```

### 23.4 Compound option sub-fields

```
--filter name=blur,strength=5,enabled=true
--worker name=RENDER_1,policy=POLICY_ROUND,level=80,budget=256KiB
```

### 23.5 Help and version

Implicit options (suppressible via `meta.implicit_help = false`):

```
-h, --help[=NAME]           Print help; NAME scoped to section/group/option
    --help-detail[=NAME]    Like --help but includes detail-visible options
-V, --version               Print version and exit
```

`NAME` is matched case-insensitively. Ambiguous matches print a disambiguation list.

### 23.6 Response files

`-C <file>` and `@<file>` splice file contents into argv.

---

## 24. Reserved keywords

The following identifiers may not be used as option names, type names, or section titles. Using them as identifiers is a parse error.

**Structural**

```
meta  section  option  group  subcommand  positional  i18n
```

**Directives**

```
@schema  @import
```

**Meta fields**

```
app  brief  version  author  prefix  output  kind  i18n
doc-title  description  implicit_help
```

**Option fields**

```
type  short  alias  default  required  multiple  visible
help  details  note  example  since  display-unit
sensitive  deprecated  depends-on  conflicts  allowed
unique  in  min  max  matches  owner
```

**Group fields**

```
options  mandatory
```

**Condition keywords** (not usable as identifiers)

```
ifdef  ifndef  ifkey  ifnkey  if
```

**Built-in types**

```
bool  flag  string  file  dir  path
int  int8  int16  int32  int64
uint8  uint16  uint32  uint64
float  double
duration  bytes  frequency  ratio
sint8  sint16  sint32  sint64
```

**Built-in literals**

```
true  false  on  off
optional  mandatory
all  detail  never
application  library
as  dynamic
```

**Gengetopt compat aliases** (reserved but map to cliforge equivalents)

```
long  unsigned  short (as type — use int16 instead)
```

---

## 25. Schema versioning policy

`@schema cliforge v1` is the contract anchor. Within `v1`:

- New qualifiers are added only when no existing schema uses them as identifiers.
- Defaults and generated ABI are stable.
- Breaking changes go in `v2`.

The schema version is embedded in generated code. The generator accepts both `v1` and `v2` schemas simultaneously.

---

## 26. v1 deliverables vs v2 roadmap

### 26.1 In v1

- All primitive, quantity, choice, and compound types.
- All option qualifiers in §10.
- Sections and groups.
- `@import` with mandatory alias; help + namespacing only.
- Three-tier condition system: `ifdef`/`ifndef`, `ifkey`/`ifnkey`, `if`.
- `allowed` blocks with both build-time and runtime conditions.
- Variant-keyed defaults.
- Documentation generation: `cmdline.md` chapter.
- Scoped `--help[=NAME]` and `--help-detail`.
- Sensitive options, logging callback.
- Positional arguments (single, named, variadic).
- Subcommands (git-style).
- Response files (`-C file`, `@file`).
- i18n hook (pluggable translator + `cliforge-i18n-extract`).
- Man-page generator (`cliforge man calctool.cf`).
- LSP / VS Code extension.
- C89/C99/C11 codegen, no-stdio mode, MISRA-clean output.
- CMake module (`cliforge.cmake`).
- Packaging: `.deb`, `.rpm`.

### 26.2 Deferred to v2

- Dynamic storage (`multiple = dynamic`; heap-allocated strings).
- Rust binding generator (`cmdline.rs`).
- Shell completion generator (bash, zsh, fish).
- Env-var fallback for options.
- Config-file fallback (typed key/value, distinct from response files).
- gengetopt migration tool (`cliforge import-ggo legacy.ggo`).
- JSON dump of the parsed struct for tooling.

### 26.3 Out of scope

- User-supplied C validator callbacks.

---

## 27. Worked examples

Two complete examples are provided under `examples/`:

**`examples/calctool/`** — the primary reference. A filter-capable expression evaluator that exercises every v1 feature. Uses the `calctool` domain (generic mathematical operations) to keep examples publicly shareable.

**`examples/reference/`** — a secondary reference using a data-processing pipeline tool (`datapipe`) as the application domain. Demonstrates `@import` with statically-linked and dynamically-loaded libraries, build-time conditional sections, and every group/dependency feature.

Each directory contains a `README.md` with build instructions and feature coverage tables.

---

*End of cliforge schema specification v1 draft 0.3*
