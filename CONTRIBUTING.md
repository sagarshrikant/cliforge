# Contributing to cliforge

Thank you for your interest in contributing. This document covers the build
setup, coding standards, test requirements, and pull-request process.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Developing in a container](#developing-in-a-container)
3. [Building from source](#building-from-source)
4. [Running the test suite](#running-the-test-suite)
5. [Coverage report](#coverage-report)
6. [AI test advisor](#ai-test-advisor)
7. [Coding standards](#coding-standards)
8. [Test policy](#test-policy)
9. [Pull request checklist](#pull-request-checklist)
10. [Project layout](#project-layout)

---

## Prerequisites

cliforge has **zero runtime dependencies** and needs no external C libraries.
The tools below are purely the build / test / docs / packaging toolchain. If you
would rather not install anything on your host, skip straight to
[Developing in a container](#developing-in-a-container) — the dev container
ships everything below pre-installed.

### Required (build + test)

| Tool | Minimum | Purpose |
|------|---------|---------|
| CMake | 3.16 | Build system |
| Ninja | 1.10 | Default generator used by the VSCode tasks and CI |
| GCC **or** Clang | GCC 7 / Clang 6 | C compiler for cliforge and the generated code |
| g++ / clang++ | same | C++ compiler — only needed to build the GTest suite |
| GDB | 10 | Step-debugging via the F5 launch configs |
| Git | any | |

### Optional (only for specific tasks)

| Tool | Needed for |
|------|-----------|
| lcov + genhtml | Coverage HTML report (`-DCLIFORGE_COVERAGE=ON`) |
| valgrind | Memory checking |
| Python 3 (≥ 3.9) + pip | AI test advisor (`tools/utest_agent/`) |
| doxygen + graphviz | API reference extraction (+ diagrams) |
| Sphinx + Breathe + Furo (pip) | Reference-guide documentation site |
| dpkg-dev + fakeroot | Building / testing the `.deb` package |
| rpm | Building / testing the `.rpm` package |

### One-line install (Debian / Ubuntu / WSL)

This installs everything required **plus** the common optional tools — i.e. the
same set the dev container bundles, so a native host reaches parity with CI:

```sh
sudo apt update && sudo apt install -y \
    build-essential gcc g++ clang \
    cmake ninja-build gdb lcov valgrind \
    python3 python3-pip python3-venv \
    doxygen graphviz \
    dpkg-dev fakeroot rpm zstd \
    git curl

# Python docs toolchain + AI-advisor dependencies
pip3 install --break-system-packages sphinx breathe furo sphinx-copybutton myst-parser
pip3 install --break-system-packages -r tools/utest_agent/requirements.txt
```

> **Fedora / RHEL:** the package names differ slightly (`gcc-c++`, `ninja-build`,
> `dpkg`, `rpm-build`); install the equivalents of the list above.

The exact, authoritative list always lives in
[`.devcontainer/Dockerfile`](.devcontainer/Dockerfile) — if the two ever drift,
the Dockerfile wins.

---

## Developing in a container

The repository ships a ready-to-use development container under
`.devcontainer/`. It is built on **Ubuntu 22.04 — the same base as our CI
runners** — so a green build inside the container means a green build in CI.
Think of it as a CI runner you can open a shell inside.

The image bundles the full toolchain (`gcc`, `clang-14`, `cmake`, `ninja`,
`gdb`, `lcov`, `valgrind`), the documentation toolchain (`doxygen`, `graphviz`,
Sphinx + Breathe + Furo), the AI test advisor's Python dependencies, the Ollama
binary (offline advisor backend), and packaging tools (`dpkg-dev`, `fakeroot`,
`rpm`) for testing `.deb`/`.rpm` builds locally.

### Option A — VSCode Dev Containers (recommended)

1. Install the **Dev Containers** extension (`ms-vscode-remote.remote-containers`).
2. Open the cliforge folder, then run **"Dev Containers: Reopen in Container"**
   from the command palette (`Ctrl+Shift+P`).
3. VSCode builds the image, mounts the repo at `/workspaces/cliforge`, and
   installs the C/C++, CMake, and Python extensions automatically.
4. Press **F5** to build and debug (see the launch configs below), or run any
   task from **Terminal → Run Task**.

### Option B — plain Docker

```sh
# Build the image (run from the repo root)
docker build -t cliforge-dev -f .devcontainer/Dockerfile .

# Open a shell with the repo bind-mounted; --cap-add=SYS_PTRACE enables gdb
docker run --rm -it \
    --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
    -v "$PWD":/workspaces/cliforge -w /workspaces/cliforge \
    cliforge-dev

# Inside the container, the normal build/test flow just works:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCLIFORGE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Ollama (offline AI advisor)

To keep the image small, only the Ollama **binary** is baked in — the ~5 GB
model is not. Pull it once, inside the running container:

```sh
bash tools/utest_agent/setup_ollama.sh     # installs the service + pulls the model
```

The Claude backend needs no download — just `export ANTHROPIC_API_KEY=...`.

### IDE build, test, and debug (VSCode)

The `.vscode/` configs work both inside the container and on a host that has
the toolchain installed.

**Tasks** (`Ctrl+Shift+P → Tasks: Run Task`):

| Task | What it does |
|------|--------------|
| `cliforge: build (Debug + tests)` | Default build (`Ctrl+Shift+B`) → `build/`, debuggable, tests on |
| `cliforge: build (Release)` | Optimised build → `build-release/` |
| `cliforge: configure (Debug + coverage)` | Adds gcov/lcov instrumentation to `build/` |
| `cliforge: run all tests` | Builds, then runs the full ctest suite |
| `cliforge: run unit tests only` / `… system tests only` | Labelled ctest subsets |

**Debug** (press **F5**, configurations in `.vscode/launch.json`):

| Configuration | Use |
|---------------|-----|
| `Debug cliforge (calctool.cf)` | Step through the generator on the canonical example |
| `Debug cliforge (current .cf file)` | Run the generator on whatever `.cf` is open in the editor |
| `Debug unit test (pick binary)` | Pick `ut_cf_lex`/`ut_cf_parse`/`ut_cf_gen`/`ut_cf_util`, optionally a `--gtest_filter` |
| `Attach to process (gdb)` | Attach GDB to an already-running binary |

Each debug config first runs the Debug build task, so the binary you step
through always matches your latest source.

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
      -DCLIFORGE_COVERAGE=ON        \
      -DCLIFORGE_BUILD_TESTS=ON

cmake --build build-cov -j$(nproc)
ctest --test-dir build-cov -L coverage
cmake --build build-cov --target cliforge_coverage

# View: build-cov/coverage-report/index.html
```

The `cliforge_coverage` target runs all tests, captures `.gcda` data, strips
noise (system headers, gtest internals), and generates an HTML report via
`genhtml`.

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

### Coverage target

The project targets **100% line coverage** on all source files under `src/`.
The CI coverage step will fail if any reachable line is uncovered.

### Justifying exclusions

If a line is genuinely unreachable in a testable build (defensive assert,
OS-error branch), mark it with `/* LCOV_EXCL_LINE */` and add a comment
explaining why:

```c
if (fclose(fp) != 0)
    return -1; /* LCOV_EXCL_LINE: OS error only */
```

Do not use `LCOV_EXCL_LINE` to hide untested code paths that should be tested.

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
- [ ] New public functions have Doxygen blocks.
- [ ] New schema features have unit tests and, where appropriate, system tests.
- [ ] `LCOV_EXCL_LINE` lines have a justifying comment.
- [ ] No dynamic allocation added to `src/` or to generator output templates.
- [ ] Generated code still compiles under `gcc -std=c89 -Wall -Wextra
  -Wpedantic` (run the `st_generated_code` system tests).
- [ ] Commit messages are imperative-mood one-liners (`Add`, `Fix`, `Remove`,
  `Refactor`), followed by a blank line and an explanatory paragraph if needed.

---

## Project layout

```
cliforge/
├── .devcontainer/
│   ├── Dockerfile          # Ubuntu 22.04 dev image (mirrors CI + dev tools)
│   └── devcontainer.json   # VSCode "Reopen in Container" definition
├── .vscode/
│   ├── tasks.json          # Build / test / coverage / utest_agent tasks
│   ├── launch.json         # F5 GDB debug configs (generator + unit tests)
│   ├── c_cpp_properties.json # IntelliSense (compile_commands.json driven)
│   └── settings.json       # Editor + cSpell settings for .cf files
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
