# Change Log

All notable changes to the **cliforge** VS Code extension will be documented in this file.

## [0.3.0] - 2026-06-16

### Added
- Support for **`@schema cliforge v2`** schemas (no longer flagged as an unknown
  version).
- Recognise the v2 qualifiers **`on-error`** (option + `meta` default) and
  **`units [ … ]`** on quantity types: syntax highlighting, completion, hover
  docs, and no more "unknown qualifier" errors.
- The type-expression parser now accepts `units [ a, b, c ]` after a quantity
  type, so v2 schemas parse without spurious diagnostics.

### Fixed
- The "quantity needs a display hint" warning is suppressed when the type
  already restricts its units via `units [ … ]`.
- Guarded a crash in the unit-hint validation when a quantity type had no
  parenthesised unit argument.

## [0.2.1] - 2026-05-27

### Fixed
- No extension changes — version bump to align with cliforge tool release.

## [0.2.0] - 2026-05-27

### Fixed
- False-positive `missing-quantity-display-unit` diagnostic: warning no longer
  fires when a `display-unit` qualifier is already present on the option.

## [0.1.0] - 2026-05-20

### Added
- Initial release.
- TextMate grammar for `.cf` files (cliforge schema v1).
- Language configuration: bracket matching, comment toggles, auto-close pairs.
- Snippets for common blocks: `meta`, `section`, `option`, `struct`, `enum`, `group`, `@import`, variant-keyed defaults.
- Language Server features:
  - Diagnostics: missing/incorrect `@schema` directive, unknown keywords, mandatory `@import` alias, duplicate aliases and option names, reserved-keyword misuse, missing `default:` arm in variant-keyed defaults.
  - Completion: context-aware suggestions for keywords, types, qualifiers, enum values, unit suffixes.
  - Hover: spec excerpts on keywords and built-in types.
  - Document symbols: outline view for sections, options, groups, enums, imports.
  - Go-to-definition: `@import` paths, enum references, group option references.
