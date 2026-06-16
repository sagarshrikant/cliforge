# cliforge Reference Examples

This directory contains a complete, multi-library reference application that demonstrates
every major cliforge v1 feature in context. The example domain is a **data-processing
pipeline tool** (`datapipe`) with a static I/O library and a dynamically-loaded
filter plugin.

## File Map

| File | Role | Import style |
|------|------|-------------|
| `app.cf` | Main application schema | root; `@import`s libio |
| `libio.cf` | Static I/O library | `@import "libio.cf" as io` |
| `libfilt.cf` | Dlopen filter/transform plugin | not imported; plugin calls its own parser |

## Feature Coverage

| Feature | Where shown |
|---------|-------------|
| Named choice type `()` | `libio.cf` → `socket-domain`; `libfilt.cf` → `passthrough-policy` |
| Inline anonymous choice `()` | `libfilt.cf` → `option format` |
| Named compound type `{}` | `app.cf` → `decoder-spec`; `libfilt.cf` → `stage-spec` |
| Quantity types (`duration`, `bytes`, `frequency`, `ratio`) | `libio.cf` § Timing, § Socket; `libfilt.cf` § Sampling |
| `depends-on` | `libio.cf` → `retry-delay`, `source-port`, `tls-key`; `libfilt.cf` → `burst` |
| `conflicts` | `app.cf` → `filter` conflicts `[proto, port, host]` |
| `allowed { }` with `ifdef` / `if` | `app.cf` → `option proto` |
| `multiple = N` | `libio.cf` → `source-ip` (4); `libfilt.cf` → `stage` (4) |
| `multiple = min..max` | `app.cf` positional args (1..32) |
| `visible = detail` | throughout (tuning knobs, low-priority options) |
| `visible = never` | `libfilt.cf` → `_force-error-rate` (test injection knob) |
| `sensitive = true` | `libio.cf` → `tls-key` |
| `deprecated` | `app.cf` → `old-verbose` |
| `ifdef` conditional option/section | `app.cf` § Threading; `libio.cf` § TLS |
| `ifkey` conditional section | `app.cf` § Hardware; `libfilt.cf` § Statistics |
| `ifkey KEY == VALUE` | `app.cf` → `ifkey target == embedded`, `ifkey target == server` |
| `ifdef A \|\| B` | `app.cf` § Threading |
| Variant-keyed `default = { ... }` | `app.cf` → `outfile` |
| `group { mandatory }` (exactly-one) | `app.cf` → `output-format` group |
| `group` (advisory) | `app.cf` → `verbosity` group |
| `@import` with alias | `app.cf` → `@import "libio.cf" as io` |
| `@import` with `ifkey` guard | `app.cf` → `@import "libfilt.cf" as filt  ifkey have-filter` |
| Response-file (`@file`, `-C file`) | `release-args.cfargs` in calctool example |
| `alias` (abbreviated long form) | `app.cf` → `alias = "dbg"` on debug-mode |
| Subcommands | not shown here — see `calctool.cf` |
| Dlopen plugin pattern | `libfilt.cf` (full description below) |

## How the dlopen plugin pattern works

Think of `libfilt.cf` like a menu card that a filter plugin publishes. The host app
(`app.cf`) does not import it — instead, the host's `--plugin` option carries a
`path=...` sub-field pointing to the `.so` file. When the plugin's init function runs,
it takes its own slice of argv and calls its own generated parser (`filt_cmdline_parse()`),
exactly as if it were a standalone program. The host and plugin never share a parser;
they only share the unparsed `char **argv` array.

```
host argv:  datapipe --io.source file.bin --plugin path=./libfilt.so,name=rate-limit \
              --filt.stage kind=filter,name=ipv4-only,weight=10 \
              --filt.rate-limit "1 MHz"

                         ┌──────────────┐        ┌──────────────────┐
  dp_cmdline_parse()     │  app parser  │  argv  │  plugin parser   │  filt_cmdline_parse()
  (from app.cf)     ───► │  consumes    │ ──────► │  consumes        │ ◄─── (from libfilt.cf)
                         │  --io.*,     │        │  --filt.stage,   │
                         │  --plugin    │        │  --filt.rate-*   │
                         └──────────────┘        └──────────────────┘
```

The plugin's `.cf` schema generates `filt_cmdline.h` and `filt_cmdline.c` that ship
with the `.so`. The host application has no compile-time knowledge of these — it only
knows the plugin's init function signature.

## Building the generated code

```sh
# Generate parsers for the main app and the static library
cliforge app.cf    -o build/
cliforge libio.cf  -o build/

# The plugin generates its own parser at plugin build time
cliforge libfilt.cf -o plugin/build/

# Build with feature key-words active
cliforge app.cf \
  --key-words="have-filter,have-tls" \
  -o build/
```

## Running datapipe (illustrative)

```sh
# Minimal — read from stdin, write to stdout
datapipe --io.source stdin

# With TLS, custom buffer, rate-limited filter plugin
datapipe --io.source file.bin \
         --io.buffer-size "4 MiB" \
         --io.tls-cert server.pem --io.tls-key server.key \
         --proto ipv4 \
         --plugin path=./libfilt.so,name=rate-limit \
         --filt.rate-limit "500 kHz" --filt.burst 128 \
         --format json --outfile results.json

# Use a response file for long option sets
datapipe @release-args.cfargs
```

## Schema syntax quick-reference

```cliforge
@schema cliforge v1

meta {
  app    = "myapp"
  prefix = "my"
  output = "cmdline"
}

/* Named choice type */
log-level = (quiet, normal, verbose, debug)

/* Named compound type */
endpoint = { host: string, port: uint16 }

section "Network" {
  description = "Network configuration."

  option level {
    type    = log-level
    default = "normal"
    help    = "Verbosity level."
  }

  option server {
    type     = endpoint
    required = mandatory
    help     = "Remote server address."
    example  = "--server host=api.example.com,port=8080"
  }

  ifdef ENABLE_TLS {
    option cert {
      type = file
      help = "TLS certificate path."
    }
  }
}
```

See `app.cf`, `libio.cf`, and `libfilt.cf` for the full feature set, and
`../../docs/spec/SPEC.md` for the complete language reference.

## v2 feature demo

`v2_features.cf` is a small, self-contained schema that demonstrates the cliforge 0.4.0 features (require `@schema cliforge v2`): unit-aware quantity types with conversion helpers, `units [..]` restriction, `on-error = exit|warn`, numeric range enforcement, upper-cased enum constants, and typed fields inside a compound record. Generate it with `cliforge v2_features.cf -o gen/` and inspect the typed `struct`s and `v2_*_to_*()` helpers in `gen/cmdline.h`.
