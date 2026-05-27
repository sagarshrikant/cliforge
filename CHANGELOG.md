# Changelog

All notable changes to cliforge are documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
cliforge uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.2.1] — 2026-05-27

### Fixed

- Coverage target (`cliforge_coverage`) now correctly runs both unit tests
  (76 tests, labeled `unit`) and system tests (11 tests, labeled `system`).
  Previously only system tests ran, underreporting line coverage.
- Fixed `gtest_discover_tests PROPERTIES LABELS` label application: switched
  from `TEST_LIST` + `set_tests_properties` (configure-time no-op) back to
  `PROPERTIES LABELS unit` (applied at POST_BUILD discovery time).
- Suppressed `geninfo` `mismatch,mismatch` warnings from GCC 13 / lcov 2.x
  gcov format changes affecting C++ test bodies.
- Suppressed `lcov` `unused,unused` pattern warnings on ARM64 hosts where
  `/usr/*` glob expands to arch-specific subdirectories with no coverage data.
- Coverage workflow (`.github/workflows/coverage.yml`) updated to use
  `CLIFORGE_COVERAGE=ON` CMake flag instead of raw compiler flags, aligning
  it with the local `cliforge_coverage` CMake target.
- Added coverage regression gate to CI: overall line coverage must not drop
  below the current floor (ratchet — floor only moves up).

### Changed

- Coverage policy updated from "100% line coverage required" to a tiered
  per-module target (see CONTRIBUTING.md § Test policy). The CI floor is
  a ratchet set to the current measured baseline.
- `CONTRIBUTING.md` coverage section corrected: removed stale `ctest -L coverage`
  command; the `cliforge_coverage` target runs tests internally.

[0.2.1]: https://github.com/sagarshrikant/cliforge/compare/v0.2.0...v0.2.1

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

[0.2.0]: https://github.com/shrikant-sagar/cliforge/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/shrikant-sagar/cliforge/releases/tag/v0.1.0
