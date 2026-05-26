# calctool — cliforge v1 reference example

`calctool` is a fictional command-line expression evaluator used throughout
the cliforge v1 specification as the canonical example. It is generic enough
to be publicly shareable and complex enough to exercise every v1 feature.

## What's in the box

| Path | Purpose |
|------|---------|
| `calctool.cf` | Main application schema. Imports four library schemas. |
| `lib/arith.cf` | Static lib — arithmetic operators (+, -, *, /). |
| `lib/cmp.cf` | Static lib — comparison operators (<, >, ==, ...). |
| `lib/trig.cf` | Shared lib — trigonometry. Conditional (`ifkey have-trig`). |
| `lib/stats.cf` | Shared lib — statistics. Conditional (`ifkey have-stats`). |
| `lib/plugin_api.cf` | **Template** for dlopen plugin authors. Not `@import`ed. |
| `src/main_static.c` | Reference consumer using static-storage mode (v1 default). |
| `src/main_dynamic.c` | Same consumer, dynamic-storage mode (v2 preview). |
| `release-args.cfargs` | Example response file (`-C` / `@` style). |

## Feature coverage

| Feature | Where |
|---------|-------|
| Named choice types `= (V1, V2)` | `verbosity`, `eval-mode` (top-level); `div-zero-policy`, `angle-unit` (section-level in lib files) |
| Named compound types `= { field: type }` | `plugin-spec` (top-level) |
| Inline compound `type = { ... }` | lib files |
| Inline choice `type = (V1, V2)` | `arith.rounding`, `stats.mode` |
| Quantity types | `deadline` (duration), `stack`/`memory-budget`/`cache` (bytes) |
| `multiple = N` | `plugin` (up to 16) |
| `multiple = min..max` | `expressions` positional (1..32) |
| `visible = detail` | `plugin-path` |
| `visible = never` | `no-sandbox` |
| `alias` | `plugin-path` alias `pp` |
| `conflicts` | `color` ↔ `json` |
| `depends-on` | (see reference/app.cf for a full example) |
| `allowed { }` | (see reference/app.cf for a full example) |
| `ifdef` build-time condition | `logfile` variant default, `Output` section |
| `ifkey` schema-key condition | `@import` of trig and stats |
| Variant-keyed `default` | `logfile` |
| `sensitive` | `api-token` |
| `deprecated` (option) | (see reference examples) |
| `deprecated` (subcommand) | `evaluate` subcommand alias |
| Sections with `description` | all sections |
| `group` with `mandatory` | `output-format` |
| `subcommand` | `eval`, `bench`, `evaluate` |
| `positional` with `multiple` | `expressions`, `plugin-name` |
| Response files | `release-args.cfargs` |
| `display-unit` | `deadline` |
| `help`, `details`, `note`, `example`, `since` | throughout |
| `i18n` | `meta.i18n = "po/"` |
| Library import + namespacing | `--arith.*`, `--cmp.*`, `--trig.*`, `--stats.*` |

## How to generate (once the generator exists)

```sh
# Export compiler flags — cliforge reads CFLAGS/CPPFLAGS automatically
export CFLAGS="-DLINUX_BUILD -O2"

# Generate sources from each .cf
cliforge --key-words="have-trig,have-stats" calctool.cf -o build/
cliforge lib/arith.cf  -o build/
cliforge lib/cmp.cf    -o build/
cliforge lib/trig.cf   -o build/
cliforge lib/stats.cf  -o build/

# Build static libs
cc -c build/cmdline_arith.c && ar rcs libarith.a cmdline_arith.o
cc -c build/cmdline_cmp.c   && ar rcs libcmp.a   cmdline_cmp.o

# Build shared libs
cc -shared -fPIC build/cmdline_trig.c  -o libtrig.so
cc -shared -fPIC build/cmdline_stats.c -o libstats.so

# Build calctool binary
cc -std=c99 -Wall -O2 \
   src/main_static.c build/cmdline.c \
   libarith.a libcmp.a -L. -ltrig -lstats -ldl \
   -o calctool
```

## Try it (once built)

```sh
calctool --help                      # top-level help
calctool --help=Performance          # scoped to one section
calctool --help=plugin               # scoped to one option
calctool --help-detail               # includes detail-visible options
calctool eval --help                 # subcommand-specific help
calctool @release-args.cfargs eval "2 + 2"
calctool --dump eval "1+1"           # api-token masked (sensitive=true)
```
