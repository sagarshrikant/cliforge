/* ============================================================================
 *  completion.ts
 *  ----------------------------------------------------------------------------
 *  Context-aware completion for cliforge schemas.
 *
 *  Strategy: look at the few characters immediately before the cursor, plus
 *  the brace depth and the enclosing block keyword, to figure out *where* in
 *  the grammar we are and offer only the items that make sense there.
 * ========================================================================== */

import {
    CompletionItem, CompletionItemKind, MarkupKind, Position, TextDocumentIdentifier,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import {
    BLOCK_KEYWORDS, CONDITIONAL_KEYWORDS, QUALIFIER_KEYS,
    META_KEYS, PRIMITIVE_TYPES, QUANTITY_TYPES, UNIT_SUFFIXES,
    GROUP_KEYS, REQUIRED_VALUES, VISIBLE_VALUES, KEYWORD_DOCS,
} from "./keywords";
import { SchemaFile } from "./ast";

export function buildCompletions(
    _docId: TextDocumentIdentifier,
    doc: TextDocument,
    position: Position,
    file: SchemaFile | null,
): CompletionItem[] {
    const offset = doc.offsetAt(position);
    const text = doc.getText();
    const ctx = classifyContext(text, offset, file);

    switch (ctx.kind) {
        case "top-level":              return topLevelItems();
        case "inside-meta":            return metaItems();
        case "inside-option":          return optionQualifierItems();
        case "inside-group":           return groupKeyItems();
        case "after-type-equals":      return typeItems();
        case "after-required-equals":  return requiredValueItems();
        case "after-visible-equals":   return visibleValueItems();
        case "after-quantity-paren":   return quantityTargetItems(ctx.detail!);
        case "after-numeric":          return unitItems();
        default:                       return topLevelItems().concat(optionQualifierItems());
    }
}

/* ---------------------------------------------------------------------------- */

type Context =
    | { kind: "top-level" }
    | { kind: "inside-meta" }
    | { kind: "inside-option" }
    | { kind: "inside-group" }
    | { kind: "after-type-equals" }
    | { kind: "after-required-equals" }
    | { kind: "after-visible-equals" }
    | { kind: "after-quantity-paren"; detail: string }
    | { kind: "after-numeric" }
    | { kind: "unknown" };

function classifyContext(text: string, offset: number, _file: SchemaFile | null): Context {
    const before = text.slice(0, offset);

    // Detect "qualifier = " contexts (most specific, check first)
    const m = /\b([A-Za-z_][A-Za-z0-9_\-]*)\s*=\s*$/.exec(before);
    if (m) {
        const key = m[1];
        if (key === "type")     return { kind: "after-type-equals" };
        if (key === "required") return { kind: "after-required-equals" };
        if (key === "visible")  return { kind: "after-visible-equals" };
    }

    // Inside `duration(`, `bytes(`, `frequency(`, `ratio(`
    const qm = /\b(duration|bytes|frequency|ratio)\s*\(\s*$/.exec(before);
    if (qm) return { kind: "after-quantity-paren", detail: qm[1] };

    // Numeric literal just typed — offer unit suffixes
    if (/(?<![A-Za-z0-9_])(?:\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][-+]?\d+)?)$/.test(before)) {
        return { kind: "after-numeric" };
    }

    // Brace-depth heuristic: walk back through unmatched braces to find the
    // keyword that introduced the most recent enclosing block.
    // Think of it like matching parentheses: we keep a counter and look for
    // the unmatched '{' that started our current scope.
    let depth = 0;
    let blockHeader: string | null = null;
    for (let i = offset - 1; i >= 0; i--) {
        const c = text[i];
        if (c === "}") {
            depth++;
        } else if (c === "{") {
            if (depth === 0) {
                // This is the opening brace of our current scope.
                // Look back for the keyword that precedes the '{'.
                const hdr = /\b(meta|section|option|group|subcommand|positional|ifdef|ifndef|ifkey|ifnkey|allowed)\b[^{]*$/.exec(text.slice(0, i));
                blockHeader = hdr ? hdr[1] : null;
                break;
            }
            depth--;
        }
    }

    switch (blockHeader) {
        case "meta":       return { kind: "inside-meta" };
        case "option":     return { kind: "inside-option" };
        case "group":      return { kind: "inside-group" };
        case "section":    return { kind: "inside-option" };
        case "subcommand": return { kind: "inside-option" };
        // Inside a conditional block we're back at top-level scope
        case "ifdef":
        case "ifndef":
        case "ifkey":
        case "ifnkey":
            return { kind: "top-level" };
        case null:         return { kind: "top-level" };
        default:           return { kind: "unknown" };
    }
}

/* ---------------------------------------------------------------------------- */

function mk(label: string, kind: CompletionItemKind, doc?: string): CompletionItem {
    const item: CompletionItem = { label, kind };
    if (doc) {
        item.documentation = { kind: MarkupKind.Markdown, value: doc };
    }
    return item;
}

function topLevelItems(): CompletionItem[] {
    const items: CompletionItem[] = [
        mk("@schema cliforge v1", CompletionItemKind.Keyword, KEYWORD_DOCS["@schema"]),
        mk("@import", CompletionItemKind.Keyword, KEYWORD_DOCS["@import"]),
    ];

    // Structural block keywords
    for (const kw of BLOCK_KEYWORDS) {
        items.push(mk(kw, CompletionItemKind.Class, KEYWORD_DOCS[kw]));
    }

    // Conditional block keywords (ifdef / ifndef / ifkey / ifnkey)
    for (const kw of CONDITIONAL_KEYWORDS) {
        items.push(mk(kw, CompletionItemKind.Keyword, KEYWORD_DOCS[kw]));
    }

    return items;
}

function metaItems(): CompletionItem[] {
    return [...META_KEYS].map(k => mk(k, CompletionItemKind.Property, KEYWORD_DOCS[k]));
}

function optionQualifierItems(): CompletionItem[] {
    return [...QUALIFIER_KEYS].map(k => mk(k, CompletionItemKind.Property, KEYWORD_DOCS[k]));
}

function groupKeyItems(): CompletionItem[] {
    const items = [...GROUP_KEYS].map(k => mk(k, CompletionItemKind.Property, KEYWORD_DOCS[k]));
    // Also offer 'mandatory' as a bare keyword completion inside group blocks
    items.push(mk("mandatory", CompletionItemKind.Keyword, KEYWORD_DOCS["mandatory"]));
    return items;
}

function typeItems(): CompletionItem[] {
    const out: CompletionItem[] = [];

    // Primitive types
    for (const t of PRIMITIVE_TYPES) {
        out.push(mk(t, CompletionItemKind.TypeParameter, KEYWORD_DOCS[t]));
    }

    // Quantity types — offer as snippet to guide the user toward the unit
    for (const q of QUANTITY_TYPES) {
        out.push({
            label: q,
            kind: CompletionItemKind.TypeParameter,
            documentation: { kind: MarkupKind.Markdown, value: KEYWORD_DOCS[q] ?? "" },
        });
    }

    // Anonymous inline choice  (A, B, C)
    out.push({
        label: "(choice...)",
        kind: CompletionItemKind.TypeParameter,
        insertText: "($1, $2, $3)$0",
        insertTextFormat: 2,   // Snippet
        documentation: {
            kind: MarkupKind.Markdown,
            value: "Inline anonymous choice type: `(value1, value2, value3)`",
        },
    });

    // Anonymous inline compound  { field: type }
    out.push({
        label: "{compound...}",
        kind: CompletionItemKind.TypeParameter,
        insertText: "{\n  ${1:field1}: ${2:string},\n  ${3:field2}: ${4:uint16}\n}$0",
        insertTextFormat: 2,
        documentation: {
            kind: MarkupKind.Markdown,
            value: "Inline anonymous compound type: `{ field: type, ... }`",
        },
    });

    return out;
}

function requiredValueItems(): CompletionItem[] {
    return [...REQUIRED_VALUES].map(v =>
        mk(v, CompletionItemKind.EnumMember, KEYWORD_DOCS[v]));
}

function visibleValueItems(): CompletionItem[] {
    return [...VISIBLE_VALUES].map(v =>
        mk(v, CompletionItemKind.EnumMember, KEYWORD_DOCS[v]));
}

function quantityTargetItems(quantity: string): CompletionItem[] {
    switch (quantity) {
        case "duration":  return ["ns", "us", "ms", "s", "m", "h", "d"].map(u => mk(u, CompletionItemKind.Unit));
        case "bytes":     return ["B", "KiB", "MiB", "GiB", "KB", "MB", "GB", "TB", "TiB"].map(u => mk(u, CompletionItemKind.Unit));
        case "frequency": return ["Hz", "kHz", "MHz", "GHz"].map(u => mk(u, CompletionItemKind.Unit));
        case "ratio":     return ["fraction", "percent"].map(u => mk(u, CompletionItemKind.Unit));
        default:          return [];
    }
}

function unitItems(): CompletionItem[] {
    return [...UNIT_SUFFIXES].map(u => mk(u, CompletionItemKind.Unit));
}
