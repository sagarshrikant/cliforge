# cliforge User Guide

This guide is for schema authors — people writing `.cf` files to describe the
command-line interface of a C, C++, or Rust application. It covers the
day-to-day workflow and the most useful features, with practical examples
throughout.

If you want the complete formal reference for every keyword and edge case, see
[`docs/spec/SPEC.md`](../spec/SPEC.md).

---

## Table of Contents

1. [How cliforge works](#how-cliforge-works)
2. [Your first schema](#your-first-schema)
3. [Option types](#option-types)
4. [Sections](#sections)
5. [Choice types](#choice-types)
6. [Compound types](#compound-types)
7. [Quantity types with units](#quantity-types-with-units)
8. [Importing library schemas](#importing-library-schemas)
9. [Visibility and sensitivity](#visibility-and-sensitivity)
10. [Repeatable options](#repeatable-options)
11. [Subcommands](#subcommands)
12. [Build-time and schema-key conditionals](#build-time-and-schema-key-conditionals)
13. [Response files](#response-files)
14. [Tips and common patterns](#tips-and-common-patterns)

---

## How cliforge works

Think of cliforge like a compiler. You give it a blueprint (the `.cf` schema
file) and it produces ready-to-use C source code. The analogy:

```
Blueprint (.cf)  →  cliforge  →  Factory output (cmdline.c / cmdline.h)
```

The generated files drop straight into your build system with no extra
dependencies. Your application `#include`s the header, calls one parse function
in `main()`, and the rest of the file tells the rest of the story.

---

## Your first schema

A schema has two mandatory parts: a `@schema` directive and a `meta` block.

```cliforge
@schema v1

meta {
    app     = "calctool"
    brief   = "Expression evaluator"
    version = "1.0.0"
    prefix  = "CT"
    output  = "cmdline"
}
```

| Field | Purpose |
|-------|---------|
| `app` | Human-readable application name (used in `--help` output) |
| `brief` | One-line description shown on the `--help` header |
| `version` | Version string shown by `--version` |
| `prefix` | C identifier prefix for all generated symbols — keep it short (`CT`, `DP`, `MY`) |
| `output` | Base name for generated files (`cmdline` → `cmdline.c`, `cmdline.h`, `cmdline.md`) |

Generate and verify:

```sh
cliforge -o build/ calctool.cf
ls build/
# cmdline.c  cmdline.h  cmdline.md
```

---

## Option types

Add `option` blocks inside the schema (or inside a `section`). Every option
needs at minimum a `type` and a `help` string.

### Primitive types

```cliforge
option precision {
    type    = uint8
    default = 6
    short   = 'p'
    help    = "Decimal places in output (0–15)."
}

option output-file {
    type  = file
    short = 'o'
    help  = "Write results to FILE instead of stdout."
}

option label {
    type = string
    help = "Attach a human-readable label to the result."
}

option verbose {
    type  = flag
    short = 'v'
    help  = "Print evaluation trace."
}
```

Available primitive types:

| Type | C field type | Notes |
|------|-------------|-------|
| `flag` | `int` (0/1) | Boolean switch; no value argument |
| `bool` | `int` (0/1) | `true`/`false` value argument |
| `string` | `char[]` | Arbitrary text |
| `file` | `char[]` | File path (validated to exist at parse time) |
| `path` | `char[]` | Directory or file path |
| `uint8` … `uint64` | `uint8_t` … `uint64_t` | Unsigned integers |
| `sint8` … `sint64` | `int8_t` … `int64_t` | Signed integers |
| `float` | `float` | Single-precision floating-point |
| `double` | `double` | Double-precision floating-point |

### Required options

Mark an option mandatory with `required = mandatory`. The parser will exit with
an error if the user omits it:

```cliforge
option config {
    type     = file
    required = mandatory
    help     = "Configuration file path."
}
```

### Default values

```cliforge
option workers {
    type    = uint32
    default = 4
    help    = "Number of worker threads."
}

option mode {
    type    = string
    default = "normal"
    help    = "Operating mode."
}
```

Defaults appear in the `--help` output and are applied when the option is
not present on the command line.

---

## Sections

Group related options into named sections. Sections appear as headings in the
`--help` output, making large interfaces navigable.

```cliforge
section "Output" {
    description = "Control where and how results are written."

    option format {
        type    = (text, json, csv)
        default = text
        help    = "Output format."
    }

    option outfile {
        type  = file
        short = 'o'
        help  = "Write output to FILE (default: stdout)."
    }
}

section "Performance" {
    description = "Tune evaluation throughput."

    option workers {
        type    = uint32
        default = 4
        help    = "Parallel worker threads."
    }

    option cache {
        type    = bytes
        default = "64 MiB"
        help    = "Expression cache budget."
    }
}
```

Sections have no effect on the generated C struct — all options land in the
same flat `struct CT_cmdline`, regardless of section. Sections are purely a
documentation and `--help` grouping mechanism.

---

## Choice types

A choice type restricts an option to a fixed set of string values.

### Inline choice (anonymous)

Define the choices directly on the option:

```cliforge
option log-level {
    type    = (debug, info, warn, error)
    default = info
    help    = "Minimum log level."
}
```

The generated C field is `int log_level` where each value maps to a generated
integer constant. Inline choices are great for one-off options.

### Named choice (reusable)

Define the choice type at the top level and reuse it across multiple options
or libraries:

```cliforge
/* Top-level type definition */
verbosity = (quiet, normal, verbose, debug)

option app-log {
    type    = verbosity
    default = normal
    help    = "Application log verbosity."
}

option net-log {
    type    = verbosity
    default = quiet
    help    = "Network layer log verbosity."
}
```

When a library defines a named choice type, any schema that imports the library
can reference that type by its namespaced name.

---

## Compound types

A compound type groups multiple fields into a single option value, passed on
the command line as `key=value,key=value` pairs.

```cliforge
/* Named compound type */
server-addr = { host: string, port: uint16 }

option server {
    type     = server-addr
    required = mandatory
    help     = "Backend server address."
    example  = "--server host=api.example.com,port=8080"
}
```

The user types `--server host=api.example.com,port=8080` and the generated
parser fills a struct with the parsed fields. This is much more ergonomic than
three separate `--server-host`, `--server-port` flags.

Inline compound types (defined on the option directly) are also supported for
one-off use:

```cliforge
option proxy {
    type = { host: string, port: uint16, user: string }
    help = "HTTP proxy address."
}
```

---

## Quantity types with units

For options that represent physical quantities, cliforge provides unit-aware
types. The user passes a value with its unit on the command line, and the
parser normalises it to a canonical base unit in the generated struct.

```cliforge
option timeout {
    type         = duration
    default      = "500 ms"
    display-unit = ms
    help         = "Request timeout."
}

option cache {
    type         = bytes
    default      = "64 MiB"
    display-unit = MiB
    help         = "Cache budget."
}

option sample-rate {
    type         = frequency
    default      = "44100 Hz"
    display-unit = Hz
    help         = "Audio sample rate."
}
```

Available quantity types: `duration`, `bytes`, `frequency`, `ratio`.

The `display-unit` controls how the value is shown in `--help` and `--dump`
output. Internally, the parser converts to the base unit (nanoseconds, bytes,
hertz, etc.) before storing.

---

## Importing library schemas

This is cliforge's most distinctive capability. When your project is made up
of multiple libraries, each library ships its own `.cf` schema. The application
imports them with a namespace alias:

```cliforge
/* In calctool.cf */
@import "lib/arith.cf"  as arith
@import "lib/trig.cf"   as trig   ifkey have-trig
@import "lib/stats.cf"  as stats  ifkey have-stats
```

The alias becomes the option namespace prefix on the command line:

```sh
calctool --arith.rounding floor \
         --trig.angle-unit deg \
         --stats.mode trimmed
```

The application's generated struct contains nested sub-structs for each
imported library's options.

### Why this matters

Without schema composition, every library that wants configurable command-line
options has two bad choices: hardcode defaults (inflexible), or ask the
application to forward generic strings (fragile). With cliforge, the library
owns its schema; the application composes schemas at build time. The
distinction is like the difference between a library exposing typed function
parameters versus taking a `void *` config blob.

### Conditional imports with `ifkey`

```cliforge
@import "lib/trig.cf" as trig ifkey have-trig
```

The `have-trig` key is passed at generation time with `--key-words=have-trig`.
If the key is absent, the import is skipped and `--trig.*` options do not
exist in the generated parser.

---

## Visibility and sensitivity

### Visibility tiers

Not all options belong in the default `--help` output. cliforge provides three
visibility levels:

```cliforge
option thread-affinity {
    type    = string
    visible = detail
    help    = "CPU affinity mask (advanced tuning)."
}

option _inject-fault {
    type    = uint32
    visible = never
    help    = "Force error code N (test injection — never shown to users)."
}
```

| Level | Shown in `--help` | Shown in `--help-detail` |
|-------|-------------------|--------------------------|
| `all` (default) | ✓ | ✓ |
| `detail` | — | ✓ |
| `never` | — | — |

Use `detail` for tuning knobs that power users might need but beginners should
not be distracted by. Use `never` for test injection knobs, internal feature
flags, and options that exist only for QA tooling.

### Sensitive options (masked in dumps)

```cliforge
option api-token {
    type      = string
    sensitive = true
    help      = "API authentication token."
}
```

When the user calls `--dump` (or your application calls the generated dump
function), sensitive option values are replaced with `***`. This prevents
tokens and passwords from appearing in log files.

---

## Repeatable options

Use `multiple` to allow an option to be specified more than once:

```cliforge
option plugin {
    type     = file
    multiple = 16
    help     = "Load plugin from PATH (repeat up to 16 times)."
}

option expression {
    type     = string
    multiple = 1..32
    help     = "Expression to evaluate (positional, 1–32)."
}
```

The generated struct contains a fixed-size array and a count field:

```c
struct CT_cmdline {
    char     plugin[16][CF_MAX_PATH];
    unsigned plugin_count;

    char     expression[32][CF_MAX_STRING];
    unsigned expression_count;
};
```

---

## Subcommands

Subcommands give your tool a Git-style interface:

```cliforge
subcommand eval {
    brief = "Evaluate an expression."

    positional expressions {
        type     = string
        multiple = 1..32
        help     = "Expressions to evaluate."
    }

    option precision {
        type    = uint8
        default = 6
        help    = "Decimal places."
    }
}

subcommand bench {
    brief = "Benchmark expression evaluation speed."

    option iterations {
        type    = uint32
        default = 1000
        help    = "Number of iterations."
    }
}
```

Usage:

```sh
calctool eval "2 + 2" "sin(pi/4)"
calctool bench --iterations 10000
calctool eval --help
```

The generated parser fills `args.subcommand` with the parsed subcommand index
and a union containing the subcommand-specific fields.

---

## Build-time and schema-key conditionals

### `ifdef` — compile-time feature flags

```cliforge
section "TLS" {
    ifdef ENABLE_TLS {
        option cert {
            type = file
            help = "TLS certificate."
        }
        option key {
            type      = file
            sensitive = true
            help      = "TLS private key."
        }
    }
}
```

`ENABLE_TLS` is a CPPFLAGS macro (`-DENABLE_TLS`). If absent, the entire block
is dropped from the generated code, producing zero overhead.

### `ifkey` — schema-time key-words

```cliforge
ifkey have-trig {
    section "Trigonometry" {
        option angle-unit {
            type    = (rad, deg, grad)
            default = rad
            help    = "Angle unit for trig functions."
        }
    }
}
```

Key-words are passed at generation time:

```sh
cliforge --key-words="have-trig,have-stats" calctool.cf -o build/
```

Use `ifkey` for optional library support that is resolved at code-generation
time (before compilation). Use `ifdef` for things resolved at compile time.

---

## Response files

Long command lines can be saved in a file and referenced with `@`:

```sh
calctool @release-args.cfargs eval "2 + 2"
```

`release-args.cfargs`:
```
--arith.rounding=floor
--stats.mode=trimmed
--format=json
--outfile=results.json
```

Response files can contain any mix of options. They are expanded inline at
parse time, before any other processing. This is useful for CI configurations,
release profiles, and reproducible builds.

---

## Tips and common patterns

**Use sections from the start.** Even a simple tool benefits from grouping.
A `section "Output"`, a `section "Performance"`, and a `section "Debug"` cover
most tools. Adding a section later is non-breaking.

**Short options are a finite resource.** Reserve short options (`-v`, `-o`,
`-n`, `-f`) for the options users will type most often. Use the full long form
for everything else.

**Use `visible = detail` liberally.** Power users love tuning knobs, but
beginners should not be overwhelmed. Default `--help` should show only the
10–15 options that matter for everyday use.

**Name your choice types.** If you have a `(debug, info, warn, error)` choice
and it appears in more than one place, define it as a named type at the top
of the schema. This makes the schema easier to read and ensures consistent
behaviour.

**Keep library schemas independent.** A library schema (`lib/arith.cf`) should
not `@import` the application schema. The dependency goes one way: application
imports library, never the reverse.

**Check generated output.** After adding new schema features, do a quick read
of the generated `cmdline.h` and `cmdline.c` to verify the field names and
types look right. The system tests cover standard cases; your application may
have specific expectations.

**Use `example =` on compound types.** The `example` field appears in
`--help-detail` output and gives users a concrete template to copy-paste:

```cliforge
option server {
    type    = { host: string, port: uint16 }
    example = "--server host=api.example.com,port=443"
    help    = "Backend server."
}
```

---

## Next steps

- Read the canonical [calctool example](../../examples/calctool/README.md)
  to see all features working together in a realistic multi-library project.
- Consult the [Schema Language Specification](../spec/SPEC.md) for the
  complete formal definition of every keyword, constraint, and edge case.
- Check the [reference schemas](../../examples/reference/README.md) for a
  feature-complete showcase including the dlopen plugin pattern.
