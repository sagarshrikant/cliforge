# Changelog

All notable changes to cliforge are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
cliforge uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.4.0] — 2026-06-16

Minor release adding declarative validation and a richer typed schema. All new
schema syntax is gated behind `@schema cliforge v2`; `v1` schemas keep their
exact 0.3.x behaviour (generated output is byte-for-byte unchanged). The
generator accepts both `v1` and `v2`. Design notes: `docs/design/0.4.0-plan.md`.

### Added

- **`@schema cliforge v2`** — the generator reads `vN` generically and gates the
  features below on the declared version.

- **Unit-aware quantity types are now fully generated (v2).** A `duration`,
  `bytes`, or `frequency` option/field generates a typed `struct { uint64_t
  value; enum …_unit unit; }`, a unit enum, and a base-unit conversion helper
  (`<prefix>_duration_to_ns`, `<prefix>_bytes_to_bytes`,
  `<prefix>_frequency_to_hz`). The unit suffix is parsed and preserved; e.g.
  `--timeout=30s` yields `value=30, unit=S` and `..._to_ns()` returns
  `30000000000`. (In v1 these remain plain `uint64_t` with the suffix dropped.)

- **`units [ … ]` restriction + display (v2).** A quantity type may list its
  accepted unit suffixes (`period : duration units [us, ms, s]`). Disallowed
  units are rejected at parse time, the accepted set is shown in `--help`, and a
  default that uses a non-listed unit is rejected at generate time.

- **`on-error = exit | warn` (v2).** Per-option validation-failure policy with a
  project-wide default in `meta { on-error = … }` (built-in default `exit`).
  `exit` makes the parser report and return non-zero; `warn` reports, keeps the
  default / clamps, and continues. Generated code never calls `exit()` itself.

- **Numeric range enforcement (v2).** `in lo..hi` and `max =` on integer options
  now emit runtime bounds checks that apply the `on-error` policy
  (`error: --opt out of range (lo..hi)`); v1 leaves ranges unenforced.

- **Typed quantity/choice fields inside compound records (v2).** A field such as
  `period : duration` or `sched : sched_t` in a `name = { … }` compound now
  generates the real typed storage (quantity struct / enum / sized int) instead
  of an untyped placeholder, with matching per-field parsing — for single and
  repeatable (`multiple`) records alike.

### Fixed

- **Generated enum constants are now fully upper-cased** — `CC_VERBOSITY_QUIET`
  instead of `CC_verbosity_QUIET`; the `typedef` keeps its lower-case form
  (`cc_verbosity` / `cc_verbosity_t`). Fix in `cf_gen.c` `enum_val()`.

- **Latent buffer overflow for a named-choice field inside a compound** (the
  enum field was written as a string) is corrected under v2 via parse-into-temp
  then enum match.

### Changed

- **Inline choice as a compound field** now reports a clear, actionable error
  pointing to the named-choice form (`kind = (a,b,c)` then `field : kind`),
  instead of a confusing parse error.

### Tests

- New system tests: `st_v2_generated_code_c{89,99,11}` (the v2 feature schema
  compiles clean under all three standards) and `st_v2_behavior` (end-to-end
  parse, quantity conversion, enum defaults, range-warn clamping, and unit
  rejection). Suite is green at 92 tests.

---

## [0.3.0] — 2026-06-07

### Added

- **Signed APT repository on GitHub Pages** for `sudo apt install cliforge`.
  A new `apt-repo.yml` workflow runs on each version tag: it builds the `.deb`,
  merges it into the repo already published on the `gh-pages` branch, and
  regenerates + GPG-signs the metadata (`apt-ftparchive` + `gpg`), so older
  versions stay installable and `apt upgrade` picks up new ones. Packaging logic
  is now centralised in `tools/packaging/build-deb.sh` (shared by `release.yml`
  and `apt-repo.yml`); the repository assembler lives in
  `tools/apt-repo/build-apt-repo.sh`. The `.deb` remains attached to each GitHub
  Release as well, for direct `sudo apt install ./cliforge_*.deb` use.

- **Reproducible dev container** (`.devcontainer/`): An Ubuntu 22.04 image that
  mirrors the CI runners (gcc + clang-14) and adds everything a contributor
  needs locally — `cmake`, `ninja`, `gdb`, `lcov`, `valgrind`, the docs
  toolchain (`doxygen`, `graphviz`, Sphinx + Breathe + Furo), the AI test
  advisor's Python dependencies, Ollama (binary only; pull the model on first
  use), and packaging tools (`dpkg-dev`, `fakeroot`, `rpm`). Contributors can
  `docker build` it directly or use VSCode "Reopen in Container".

- **`.devcontainer/devcontainer.json`**: VSCode Dev Containers definition that
  builds the image, attaches as a non-root `dev` user, installs the C/C++,
  CMake, and Python extensions, and enables `ptrace` so GDB works in-container.

- **`.dockerignore`**: Keeps build trees, coverage output, virtualenvs, and
  node/VSIX artefacts out of the Docker build context.

- **`.vscode/launch.json`**: GDB-backed F5 debug configurations — debug the
  generator on `calctool.cf` or on the currently open `.cf` file, debug any
  unit-test binary via a picker (with an optional `--gtest_filter`), and an
  attach-to-process config.

- **`.vscode/c_cpp_properties.json`**: IntelliSense configuration driven by the
  generated `compile_commands.json`.

### Fixed

- **`-o`/`--output` was ignored when it followed the schema argument.** The CLI
  parsed argv in a single pass and generated each file the moment it was seen,
  using whatever output directory had been set *so far* — so `schema.cf -o out`
  silently wrote to the current directory while `-o out schema.cf` worked.
  `main.c` now parses all options first and generates afterwards, so option
  order no longer matters. A missing `-o` argument and a non-writable target
  directory now produce clear errors and a non-zero exit. Added the
  `st_output_dir_ordering` system test covering all three argument orderings
  (`-o` before, `-o` after, and `--output=` joined form) with a leak check.

### Changed

- **`.vscode/tasks.json`**: Reworked build/test tasks into clear Debug (`build/`)
  and Release (`build-release/`) trees, plus a coverage-instrumented configure
  and dedicated unit-/system-/all-test runners. The existing utest_agent tasks
  are unchanged.

- **`CMakeLists.txt`**: Bumped project version to 0.3.0 and enabled
  `CMAKE_EXPORT_COMPILE_COMMANDS` so editors resolve includes/macros accurately.

- **`CONTRIBUTING.md`**: Added a "Developing in a container" section covering
  the Docker and VSCode Dev Containers workflows and the F5 debug/test loop.

---

## [0.2.0] — 2026-05-27

### Added

- **AI unit test advisor** (`tools/utest_agent/`): Python tool that analyses
  `git diff` output and lcov coverage data, then suggests GTest test additions,
  removals, and updates. Supports the Claude API (cloud) and a local Ollama
  model (offline, no API key). Modes include fast diff-only analysis, full
  build+coverage pipeline, pre-PR gate (`--prepr`), and single-file scans.
  See `tools/utest_agent/README.md` for full setup and usage.

- **VSCode extension for utest_agent** (`tools/vscode-utest-extension/`):
  Adds a status bar item, right-click context menu on `.c` files, and a
  formatted report panel showing AI test suggestions inside VSCode.

- **VSCode schema extension** (`tools/vscode-schema-ext/`): Syntax
  highlighting, hover documentation, completion, and diagnostic validation
  for `.cf` schema files. Moved from `vscode-cliforge/` and renamed.

- **GitHub Actions CI** (`.github/workflows/ci.yml`): Build + unit test matrix
  covering GCC C99, GCC C11, and Clang-14 C11 on ubuntu-22.04.

- **GitHub Actions coverage** (`.github/workflows/coverage.yml`): lcov
  pipeline that uploads an HTML coverage report as a workflow artifact and
  prints a per-file summary to the GitHub step summary.

- **GitHub Actions release** (`.github/workflows/release.yml`): Triggered on
  `v*` tags. Builds a `.deb` package (fakeroot + dpkg-deb) and a `.tar.gz`
  source archive, then creates a GitHub Release with both as assets.

- **`.vscode/tasks.json`**: 14 pre-configured VSCode tasks for all utest_agent
  modes, cmake build types, and ctest runs.

- `cf_is_valid_ident()` utility in `src/cf_util.c`/`cf_util.h`: validates that
  a cliforge identifier starts with a letter or underscore and contains only
  alphanumeric characters, underscores, or hyphens.

### Fixed

- `-Wformat-truncation` warnings in `src/cf_gen.c` `fmt_help_default()`:
  replaced `%s` with `%.*s` and an explicit precision bound so GCC static
  analysis can verify no truncation occurs in the nominal case.

- `validator.ts` false-positive `missing-quantity-display-unit` diagnostic in
  the VSCode schema extension: the warning no longer fires when a `display-unit`
  qualifier is already present on the option.

- Clang-14 apt package conflict on ubuntu-22.04 CI runner: switched from
  installing the generic `clang` meta-package (which conflicts with pre-installed
  LLVM packages) to using the versioned `clang-14` binary directly.

- Pinned `actions/checkout` to `v4.2.2` and `actions/upload-artifact` to
  `v4.4.3` to silence Node.js 20 deprecation warnings from older action pins.

### Changed

- All tools consolidated under `tools/` in the mono-repo:
  `tools/utest_agent/`, `tools/vscode-schema-ext/`, `tools/vscode-utest-extension/`.
- Removed all QNX-related references from source, documentation, and tooling.

---

## [0.1.0] — 2026-04-01

### Added

- Initial release of cliforge.
- Schema lexer (`cf_lex.c`), recursive-descent parser (`cf_parse.c`), code
  generator (`cf_gen.c`), and shared utilities (`cf_util.c`).
- Full schema DSL: option types (`flag`, `string`, `path`, `file`, integer
  family, inline and named choice, compound field-set, quantity types),
  visibility tiers, `required`, `default`, `short`, `sensitive`, sections,
  `@import` with namespace aliases, subcommands, repeatable options, response
  files, `ifdef`/`ifkey` conditionals.
- Generated output: `cmdline.c`, `cmdline.h`, `cmdline.md` (Markdown man-page).
- MISRA-C-compatible generated code: static storage only, no dynamic allocation,
  no global mutable state beyond the args struct.
- C89, C99, and C11 compatibility for generated code.
- Doxygen blocks on all generated public functions.
- `calctool` multi-library example and feature-complete reference schemas.
- Schema Language Specification (`docs/spec/SPEC.md`) and User Guide
  (`docs/user-guide/index.md`).
- CMake build system with `CLIFORGE_BUILD_TESTS` and `CLIFORGE_COVERAGE` options.
- GTest unit-test suites for `cf_lex`, `cf_parse`, `cf_gen`, and `cf_util`.

[0.4.0]: https://github.com/shrikant-sagar/cliforge/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/shrikant-sagar/cliforge/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/shrikant-sagar/cliforge/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/shrikant-sagar/cliforge/releases/tag/v0.1.0
