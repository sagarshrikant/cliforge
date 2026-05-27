# Contributing to cliforge

Thank you for your interest in contributing. This document covers the build
setup, coding standards, test requirements, and pull-request process.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Building from source](#building-from-source)
3. [Running the test suite](#running-the-test-suite)
4. [Coverage report](#coverage-report)
5. [AI test advisor](#ai-test-advisor)
6. [Coding standards](#coding-standards)
7. [Test policy](#test-policy)
8. [Pull request checklist](#pull-request-checklist)
9. [Project layout](#project-layout)

---

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| CMake | 3.16 | Required |
| GCC or Clang | GCC 7 / Clang 6 | C and C++ compiler |
| g++ / clang++ | Same | Required only for building tests |
| lcov + genhtml | 1.14 | Coverage report only |
| Python 3 | 3.9 | AI test advisor only |
| Git | any | |

No external C libraries are required. cliforge has zero runtime dependencies.

---

## Building from source

```sh
git clone https://github.com/shrikant-sagar/cliforge.git
cd cliforge

# Release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Debug build (assertions enabled)
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j$(nproc)
```

The `cliforge` binary is placed in `build/` (or `build-debug/`).

### CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `CLIFORGE_BUILD_TESTS` | `OFF` | Build the unit and system test suite |
| `CLIFORGE_COVERAGE` | `OFF` | Instrument for gcov/lcov coverage |
| `CMAKE_BUILD_TYPE` | — | `Debug`, `Release`, `RelWithDebInfo` |

---

## Running the test suite

```sh
cmake -S . -B build-tests          \
      -DCMAKE_BUILD_TYPE=Debug      \
      -DCLIFORGE_BUILD_TESTS=ON

cmake --build build-tests -j$(nproc)

# Run all tests
ctest --test-dir build-tests --output-on-failure

# Run only unit tests
ctest --test-dir build-tests -L unit --output-on-failure

# Run only system tests
ctest --test-dir build-tests -L system --output-on-failure
```

All tests must pass before a PR can be merged. The CI pipeline enforces this.

---

## Coverage report

```sh
cmake -S . -B build-cov            \
      -DCMAKE_BUILD_TYPE=Debug      \
      -DCLIFORGE_COVERAGE=ON        \
      -DCLIFORGE_BUILD_TESTS=ON

cmake --build build-cov -j$(nproc)

# Single command: resets counters → runs unit + system tests → lcov → genhtml
cmake --build build-cov --target cliforge_coverage

# View: build-cov/coverage-report/index.html
```

The `cliforge_coverage` target resets counters, runs unit tests then system
tests, captures `.gcda` data, strips noise (system headers, gtest internals),
and generates an HTML report via `genhtml`.

> **Note:** Do not run `ctest` manually before the coverage target — the target
> resets counters and re-runs all tests itself. Running `ctest` first and then
> the target means the reset step wipes your data.

---

## AI test advisor

cliforge ships an AI-powered unit test advisor in `tools/utest_agent/`. It
reads your `git diff`, maps changed functions to their test folders, optionally
runs the full build→test→lcov pipeline, and produces a report recommending
which GTest cases to add, update, or remove. It supports two LLM backends:
**Claude** (cloud, best quality) and **Ollama** (local, no API key needed).

Think of it as a colleague who always reviews your diff and asks "did you write
tests for this?" — but it also checks the coverage numbers.

### Setup (Linux / WSL)

```sh
# System packages
sudo apt install -y cmake ninja-build gcc g++ lcov python3 python3-pip zstd

# Python dependencies
pip3 install --break-system-packages -r tools/utest_agent/requirements.txt

# Claude API key (if using Claude backend)
export ANTHROPIC_API_KEY=sk-ant-...

# Ollama setup — one-time, pulls ~5 GB model
bash tools/utest_agent/setup_ollama.sh
```

### Usage

```sh
# Fast: diff-only analysis (Claude)
python3 tools/utest_agent/agent.py

# Fast: diff-only analysis (Ollama, offline)
python3 tools/utest_agent/agent.py --llm ollama

# Full pipeline: build → ctest → lcov → AI report
python3 tools/utest_agent/agent.py --with-build

# Pre-PR gate: exits 1 if coverage or tests are not ready
python3 tools/utest_agent/agent.py --prepr

# Analyse a specific file
python3 tools/utest_agent/agent.py --file src/cf_util.c

# Save report
python3 tools/utest_agent/agent.py --output utest_report.md
```

See [`tools/utest_agent/README.md`](tools/utest_agent/README.md) for full
documentation including all modes, Ollama model recommendations, and VSCode
integration.

### VSCode tasks

All agent modes are wired up as VSCode tasks in `.vscode/tasks.json`.
Open them with `Ctrl+Shift+P → Tasks: Run Task → utest …`.

---

## Coding standards

### Language

All cliforge source files (`src/`) are written in **C99**. The generated output
must also compile under C89 and C11 — test this before touching `cf_gen.c`.

### Style

cliforge follows the **Linux kernel coding style**:

- Tabs for indentation, 8-space tab stops.
- Opening braces on the same line for control flow; own line for function
  definitions.
- `snake_case` for all identifiers.
- Maximum line length: 80 columns.
- No trailing whitespace.

Run `checkpatch.pl --no-tree -f src/yourfile.c` if you have the kernel tools
available.

### MISRA-C

The cliforge generator itself, and the code it generates, aim to comply with
MISRA-C:2012. Key rules that are actively enforced:

- No dynamic memory allocation (`malloc`/`calloc`/`realloc`/`free`) in
  generated code.
- No recursion in generated code.
- All variables declared at the top of their block (C89 compatibility and
  MISRA Rule 8.1).
- No fall-through in `switch` statements without an explicit comment.
- All function parameters and return values checked.

### Doxygen

Every public function in `src/` must carry a Doxygen `/** */` block covering
`@brief`, `@param`, and `@return`. Example:

```c
/**
 * @brief  Initialise a lexer context.
 *
 * @param  lex   Pointer to a caller-allocated lexer instance.
 * @param  src   Null-terminated source text.
 * @param  len   Length of @p src in bytes (excluding the null terminator).
 * @param  fname Symbolic filename used in error messages.
 */
void cf_lex_init(cf_lexer_t *lex, const char *src,
                 unsigned int len, const char *fname);
```

### No third-party dependencies

cliforge must compile with nothing beyond a standard C compiler and the C
standard library. Do not add external libraries to `src/`.

---

## Test policy

### Coverage targets

100% line coverage is not the goal. Tests written purely to hit lines add
maintenance burden without catching real bugs. The targets below reflect what
is meaningfully testable without fault-injection frameworks:

| Module | Line coverage | Function coverage | Notes |
|--------|:---:|:---:|-------|
| `cf_util.c` | ≥ 95 % | 100 % | Pure functions; fully testable |
| `cf_lex.c` | ≥ 85 % | ≥ 90 % | Token paths are enumerable |
| `cf_gen.c` | ≥ 80 % | ≥ 90 % | Many conditional output paths |
| `cf_parse.c` | ≥ 80 % | ≥ 85 % | Error-recovery branches are hard to trigger |
| **Overall** | **≥ 80 %** | **≥ 90 %** | CI enforces a floor; see below |

**The CI floor is a ratchet** — it is set to the current overall line coverage
and may only go up. A PR that drops overall coverage below the floor is
rejected. The floor is raised in `.github/workflows/coverage.yml` whenever
coverage genuinely improves.

### Exempting truly unreachable lines

If a line is genuinely unreachable in a testable build (defensive assert,
OS-level error that cannot be simulated), mark it with `/* LCOV_EXCL_LINE */`
and add a comment explaining why:

```c
if (fclose(fp) != 0)
    return -1; /* LCOV_EXCL_LINE: fclose failure requires OS-level fault injection */
```

Do not use `LCOV_EXCL_LINE` to hide untested code paths that should be tested.
Each exclusion must be justified in the PR description.

### Adding tests

- Unit tests live in `tests/unit-tests/ut_<module>/`.
- System tests live in `tests/system-tests/`.
- Every new schema feature added to `cf_parse.c` or `cf_gen.c` must have
  corresponding unit tests and, if it affects generated output, a system
  compile test.
- Test function names follow the pattern `TEST(Suite, Description_NNN)` where
  `NNN` is a zero-padded three-digit sequence number within the suite.

---

## Pull request checklist

Before opening a PR, confirm:

- [ ] `ctest --test-dir build-tests --output-on-failure` shows 0 failures.
- [ ] `cmake --build build-tests -j$(nproc)` produces zero warnings.
- [ ] `python3 tools/utest_agent/agent.py --prepr` exits 0 (or all gaps are
      justified with `LCOV_EXCL_LINE`).
- [ ] Overall line coverage has not decreased (CI coverage floor enforces this).
- [ ] New functions reach the per-module target (see Test policy table above).
- [ ] Any `LCOV_EXCL_LINE` additions include a justifying comment and are
      explained in the PR description.
- [ ] New public functions have Doxygen blocks.
- [ ] New schema features have unit tests and, where appropriate, system tests.
- [ ] No dynamic allocation added to `src/` or to generator output templates.
- [ ] Generated code still compiles under `gcc -std=c89 -Wall -Wextra
  -Wpedantic` (run the `st_generated_code` system tests).
- [ ] Commit messages are imperative-mood one-liners (`Add`, `Fix`, `Remove`,
  `Refactor`), followed by a blank line and an explanatory paragraph if needed.

---

## Project layout

```
cliforge/
├── src/
│   ├── cf_lex.c/h        # Schema lexer
│   ├── cf_parse.c/h      # Recursive-descent parser → AST
│   ├── cf_gen.c/h        # Code generator (C + Markdown output)
│   ├── cf_ast.h          # AST node types
│   └── cf_util.c/h       # Shared utility functions
├── include/
│   └── cliforge_version.h.in   # CMake-filled version header template
├── tests/
│   ├── unit-tests/
│   │   ├── ut_cf_lex/    # Lexer tests
│   │   ├── ut_cf_parse/  # Parser tests (meta, option, import)
│   │   ├── ut_cf_gen/    # Generator tests
│   │   └── ut_cf_util/   # Utility function tests
│   └── system-tests/
│       ├── fixtures/     # .cf schema fixtures (valid and deliberately broken)
│       ├── st_pipeline/  # End-to-end: schema → generate → file existence
│       ├── st_error_cases/    # Error schemas must exit non-zero with stderr
│       └── st_generated_code/ # Generated .c compiles under C89/C99/C11
├── docs/
│   ├── spec/             # Schema language specification (SPEC.md)
│   └── user-guide/       # Practical authoring guide (index.md)
├── examples/
│   ├── calctool/         # Canonical multi-library example
│   └── reference/        # Feature-complete reference schemas
├── .github/
│   └── workflows/
│       ├── ci.yml        # Build + test matrix (gcc C99/C11, clang-14 C11)
│       ├── coverage.yml  # lcov report on every push to main
│       └── release.yml   # .deb + .tar.gz on version tag push
└── tools/
    ├── utest_agent/           # AI-powered unit test advisor (Claude + Ollama)
    ├── vscode-schema-ext/     # VSCode LSP extension for .cf schema files
    └── vscode-utest-extension/ # VSCode UI wrapper for utest_agent
```
