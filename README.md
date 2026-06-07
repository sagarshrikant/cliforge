# cliforge

**Schema-driven command-line parser generator for C, C++, and Rust.**

> **🚀 Release 0.2.0** — AI-powered unit test advisor (`tools/utest_agent`),
> full CI/CD pipeline, VSCode schema extension, and coverage reporting.
> See [CHANGELOG.md](CHANGELOG.md) for details.

cliforge reads a declarative schema file (`.cf`) and emits a self-contained
`cmdline.c` / `cmdline.h` pair that parses `argc`/`argv` with no runtime
dependencies. The generated code is fully compatible with C89, C99, and C11,
passes MISRA-C guidelines, and follows Linux kernel coding conventions.

```
calctool.cf  ──►  cliforge  ──►  cmdline.c
                              ──►  cmdline.h
                              ──►  cmdline.md   (Markdown man-page)
```

---

## Why cliforge?

Command-line parsing is boilerplate. Writing it by hand for every library in a
multi-component project is error-prone and inconsistent. cliforge makes the
schema the single source of truth — change the schema, regenerate, done.

Key capabilities beyond traditional parser generators:

- **Multi-library schema composition.** A library ships its own `.cf` schema.
  Applications `@import` it with a namespace alias (`--arith.*`, `--trig.*`),
  keeping each library's options cleanly separated.

- **Rich type system.** Beyond `string`, `flag`, and integers, the schema
  supports inline and named choice types `(a, b, c)`, compound field-set types
  `{ host: string, port: uint16 }`, and quantity types with SI/binary units
  (`"4 MiB"`, `"500 kHz"`, `"100 ms"`).

- **MISRA-C / safety-profile output.** The generated `.c` file uses only static
  storage, avoids dynamic allocation, and includes no global mutable state
  beyond the single args struct. Suitable for safety-critical and embedded
  firmware.

- **All three C standards.** The generated code compiles clean under
  `gcc -std=c89`, `-std=c99`, and `-std=c11` with `-Wall -Wextra -Wpedantic`.

- **Visibility tiers.** Options can be `visible = all` (default), `visible =
  detail` (shown only under `--help-detail`), or `visible = never` (test
  injection knobs, never shown to end users).

- **Doxygen + Sphinx ready.** Every generated function carries a Doxygen block.
  The companion `.md` file gives you a Markdown man-page for Sphinx-Breathe
  integration.

- **VSCode extension.** Syntax highlighting, hover docs, and schema validation
  for `.cf` files — see [`tools/vscode-schema-ext/`](tools/vscode-schema-ext/).

- **AI unit test advisor.** `tools/utest_agent` analyses git diffs and lcov
  coverage data and suggests GTest additions, removals, and updates using the
  Claude API or a local Ollama model. Works offline — no API key required when
  using Ollama. See [`tools/utest_agent/README.md`](tools/utest_agent/README.md).

---

## Quick start

### 1 — Install

**Via apt (recommended):**

```sh
# One time: add the signing key and repository
curl -fsSL https://shrikant-sagar.github.io/cliforge/cliforge-archive-keyring.gpg \
  | sudo tee /usr/share/keyrings/cliforge-archive-keyring.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/cliforge-archive-keyring.gpg] https://shrikant-sagar.github.io/cliforge stable main" \
  | sudo tee /etc/apt/sources.list.d/cliforge.list

sudo apt update && sudo apt install cliforge   # → /usr/bin/cliforge
```

Updates then arrive through the usual `sudo apt update && sudo apt upgrade`.

**Direct `.deb` (any Debian/Ubuntu, no repo needed):** grab the package from the
[latest release](https://github.com/shrikant-sagar/cliforge/releases/latest) and:

```sh
sudo apt install ./cliforge_*_amd64.deb
```

**From source:**

```sh
# requires cmake >= 3.16, gcc or clang
git clone https://github.com/shrikant-sagar/cliforge.git
cd cliforge
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cmake --install build          # installs to /usr/local/bin/cliforge
```

> Don't mix install methods: a source install lands in `/usr/local/bin` and will
> shadow an apt-installed `/usr/bin/cliforge` on `PATH`. Pick one, or remove the
> other (`sudo rm /usr/local/bin/cliforge`).

### 2 — Write a schema

`calctool.cf`:

```cliforge
@schema v1

meta {
    app     = "calctool"
    brief   = "Expression evaluator"
    version = "1.0.0"
    prefix  = "CT"
    output  = "cmdline"
}

option precision {
    type    = uint8
    default = 6
    short   = 'p'
    help    = "Decimal places in output (0–15)."
}

option verbose {
    type = flag
    short = 'v'
    help = "Print evaluation trace."
}

option format {
    type    = (decimal, hex, sci)
    default = decimal
    help    = "Output number format."
}
```

### 3 — Generate

```sh
cliforge -o build/ calctool.cf
# Produces: build/cmdline.c  build/cmdline.h  build/cmdline.md
```

### 4 — Use in your application

```c
#include "cmdline.h"

int main(int argc, char *argv[])
{
    struct CT_cmdline args;
    int rc = CT_cmdline_parse(argc, argv, &args);
    if (rc != 0)
        return rc;

    if (args.verbose)
        trace_enable();

    run_calculator(&args);
    return 0;
}
```

```sh
gcc -std=c99 -Wall main.c build/cmdline.c -I build/ -o calctool
```

---

## Schema features at a glance

| Feature | Syntax |
|---------|--------|
| Integer types | `uint8`, `uint16`, `uint32`, `uint64`, `sint8` … |
| Flag (bool switch) | `type = flag` |
| String / path / file | `type = string`, `type = path`, `type = file` |
| Inline choice | `type = (debug, info, warn, error)` |
| Named choice (reusable) | `log-level = (debug, info, warn, error)` |
| Compound type | `endpoint = { host: string, port: uint16 }` |
| Quantity with unit | `type = duration`, `type = bytes`, `type = frequency` |
| Mandatory option | `required = mandatory` |
| Default value | `default = 4` |
| Short option | `short = 'p'` |
| Visibility control | `visible = all \| detail \| never` |
| Sensitive (masked in dump) | `sensitive = true` |
| Sections | `section "Performance" { … }` |
| Multi-library import | `@import "lib/arith.cf" as arith` |
| Subcommands | `subcommand eval { … }` |
| Repeatable options | `multiple = 4`, `multiple = 1..8` |
| Response files | `calctool @args.cfargs` |
| Build-time conditionals | `ifdef ENABLE_TLS { … }` |
| Schema-key conditionals | `ifkey have-stats { … }` |

---

## Multi-library composition

The killer feature for projects with many components. Each library ships its
own `.cf` schema; the application imports them with namespace aliases:

```cliforge
@import "lib/arith.cf"  as arith
@import "lib/trig.cf"   as trig   ifkey have-trig
@import "lib/stats.cf"  as stats  ifkey have-stats
```

The generated parser exposes each library's options under its namespace prefix
(`--arith.rounding`, `--trig.angle-unit`, `--stats.mode`), so option names
never collide even when libraries evolve independently.

---

## Generated output quality

- Zero heap allocation — the args struct is caller-supplied.
- No global mutable state outside the args struct.
- All string fields are bounded `char[]` arrays — no `char *` pointers.
- Compiles under `-std=c89` with no VLAs, no `//` comments, no C99 extensions.
- Every public function has a Doxygen `/** */` block.
- `--help` output is formatted to 80 columns with section grouping.

---

## Project layout

```
cliforge/
├── src/                          # cliforge generator source (pure C)
│   ├── cf_lex.c/h                # Schema lexer
│   ├── cf_parse.c/h              # Recursive-descent parser → AST
│   ├── cf_gen.c/h                # Code generator (C + Markdown)
│   └── cf_util.c/h               # Shared utilities
├── tests/
│   ├── unit-tests/               # GTest suites per source file
│   └── system-tests/             # Black-box binary + compile tests
├── docs/
│   ├── spec/                     # Full schema language reference (SPEC.md)
│   └── user-guide/               # Practical authoring guide (index.md)
├── examples/
│   ├── calctool/                 # Canonical multi-library example
│   └── reference/                # Feature-complete reference schemas
├── tools/
│   ├── utest_agent/              # AI unit test advisor (Python, Claude/Ollama)
│   ├── vscode-schema-ext/        # VSCode extension for .cf schema files
│   └── vscode-utest-extension/   # VSCode extension for utest_agent reports
└── .github/workflows/
    ├── ci.yml                    # Build + unit tests (gcc/clang matrix)
    ├── coverage.yml              # lcov coverage report
    └── release.yml               # .deb + .tar.gz GitHub release
```

---

## Documentation

| Document | Audience |
|----------|---------|
| [Schema Language Specification](docs/spec/SPEC.md) | Schema authors, tool integrators |
| [User Guide](docs/user-guide/index.md) | First-time schema authors |
| [calctool example](examples/calctool/README.md) | Hands-on multi-library walkthrough |
| [Reference schemas](examples/reference/README.md) | Feature-complete schema showcase |
| [AI test advisor](tools/utest_agent/README.md) | Contributors, CI users |
| [VSCode extension](tools/vscode-schema-ext/README.md) | Schema authors using VSCode |
| [Contributing](CONTRIBUTING.md) | Contributors |
| [Changelog](CHANGELOG.md) | All users |

---

## Building and testing

```sh
# Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Build + run all 88 tests
cmake -S . -B build-tests -DCLIFORGE_BUILD_TESTS=ON
cmake --build build-tests -j$(nproc)
ctest --test-dir build-tests --output-on-failure

# Coverage report (requires lcov)
cmake -S . -B build-cov -DCLIFORGE_COVERAGE=ON -DCLIFORGE_BUILD_TESTS=ON
cmake --build build-cov -j$(nproc)
ctest --test-dir build-cov -L coverage
cmake --build build-cov --target cliforge_coverage
# HTML report: build-cov/coverage-report/index.html
```

---

## C++ and Rust usage

The generated `cmdline.c`/`cmdline.h` are plain C — usable from C++ directly
(`extern "C"` not required for the struct; the header is self-contained). For
Rust, use `bindgen` on the generated header to produce FFI bindings.

---

## License

MIT — see [LICENSE](LICENSE).
