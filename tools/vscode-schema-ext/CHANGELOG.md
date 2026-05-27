# Change Log

All notable changes to the **cliforge** VS Code extension will be documented in this file.

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
