# cliforge — VS Code extension

Language support for **cliforge** schema files (`.cf`): syntax highlighting,
snippets, diagnostics, completion, hover, document symbols, and
go-to-definition.

> The cliforge project itself is a code generator that turns a declarative
> `.cf` schema into portable C source (`cmdline.c` + `cmdline.h`). See the
> [main repo](https://github.com/shrikant-sagar/cliforge) and the
> [schema spec (v1)](https://github.com/shrikant-sagar/cliforge/blob/main/docs/spec/SPEC.md).

## Features

| | |
| - | - |
| Syntax highlighting | TextMate grammar for keywords, doc comments, types, quantity literals, unit suffixes, and operators. |
| Snippets | `schema`, `meta`, `section`, `option`, `option-struct`, `enum`, `group-exclusive`, `import-when`, variant-keyed defaults — and more. |
| Diagnostics | Missing `@schema` directive, mandatory `@import` alias, unknown qualifiers, duplicate option names, missing `default:` arm in variant-keyed defaults, reserved-keyword identifiers, group reference resolution, unit-group mismatches in defaults. |
| Completion | Context-aware: top-level keywords at file scope, meta keys inside `meta { }`, qualifier keys inside `option { }`, types after `type =`, unit suffixes after numeric literals, valid target units inside `duration(`, `bytes(`, `frequency(`, `ratio(`. |
| Hover | Hover any keyword, type, or qualifier and read a Markdown excerpt of the spec. |
| Outline | Document symbol tree for sections, options, groups, enums, and imports. |
| Go-to-Definition | Jump to imported `.cf` files, enum declarations, and option declarations referenced from group `options = [...]`. |

## Getting started

1. Install the extension (see install options below).
2. Open any file ending in `.cf`. The status bar should show **cliforge**.
3. Try typing `schema` at the top of a blank file and accept the snippet.

## Install options

### From the VS Code Marketplace

Once published:

```
ext install shrikant-sagar.vscode-cliforge
```

### Sideload a `.vsix`

```sh
git clone https://github.com/shrikant-sagar/cliforge
cd cliforge/vscode-cliforge
npm install
npm run package          # produces vscode-cliforge-<version>.vsix
code --install-extension vscode-cliforge-*.vsix
```

### Develop locally

```sh
npm install
npm run watch            # in one terminal — incremental TypeScript build
# then press F5 in VS Code to launch an Extension Development Host
```

The development host opens with the extension activated; open
`examples/sample.cf` to exercise every feature.

## Configuration

| Setting | Default | Description |
| - | - | - |
| `cliforge.validation.enable` | `true` | Toggle the diagnostics layer. Keep this on while authoring schemas; disable only if a parser bug is producing noisy false positives. |
| `cliforge.imports.followAcrossFiles` | `true` | Resolve `@import` paths against the importing file's directory for go-to-definition. |
| `cliforge.trace.server` | `off` | Log LSP traffic between the client and server (`off` / `messages` / `verbose`). Useful when filing bugs. |

## Roadmap

The extension's roadmap tracks the cliforge schema spec:

* v0.1 — current release: TextMate grammar + LSP for schema v1 (this document).
* v0.2 — cross-file imports: load `@import`ed schemas into the workspace symbol
  table for richer completion and find-all-references on `<alias>.<option>`
  references.
* v0.3 — formatting provider, code actions for common errors (insert missing
  alias, add `default:` arm, rename reserved-keyword identifiers).
* v0.4 — track schema v2 once the generator adopts it.

## Filing issues

Issues, feature requests, and PRs are welcome on the
[cliforge issue tracker](https://github.com/shrikant-sagar/cliforge/issues).
When reporting a diagnostic-related issue, please include the offending
schema snippet and the `code` field shown in the Problems pane (e.g.
`missing-import-alias`).

## License

MIT.
