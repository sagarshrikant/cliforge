/* ============================================================================
 *  symbols.ts
 *  ----------------------------------------------------------------------------
 *  Build a DocumentSymbol tree for the Outline / Breadcrumbs view.
 * ========================================================================== */

import { DocumentSymbol, SymbolKind } from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { SchemaFile, OptionDecl, NamedTypeDecl, Range as AstRange } from "./ast";

export function buildSymbols(doc: TextDocument, file: SchemaFile): DocumentSymbol[] {
    const out: DocumentSymbol[] = [];

    if (file.directive) {
        out.push({
            name: `@schema ${file.directive.language} ${file.directive.version}`,
            kind: SymbolKind.Namespace,
            range: toLspRange(doc, file.directive.range),
            selectionRange: toLspRange(doc, file.directive.range),
        });
    }

    if (file.meta) {
        const appEntry = file.meta.entries.find(e => e.key === "app");
        const appDetail = appEntry?.value.kind === "Literal"
            ? (appEntry.value as { stringValue?: string }).stringValue ?? ""
            : "";
        out.push({
            name: "meta",
            detail: appDetail,
            kind: SymbolKind.Namespace,
            range: toLspRange(doc, file.meta.range),
            selectionRange: toLspRange(doc, file.meta.range),
            children: file.meta.entries.map(e => ({
                name: e.key,
                kind: SymbolKind.Field,
                range: toLspRange(doc, e.range),
                selectionRange: toLspRange(doc, e.keyRange),
            })),
        });
    }

    for (const imp of file.imports) {
        out.push({
            name: `@import ${imp.path}${imp.alias ? ` as ${imp.alias}` : ""}`,
            kind: SymbolKind.Module,
            range: toLspRange(doc, imp.range),
            selectionRange: toLspRange(doc, imp.aliasRange ?? imp.pathRange),
        });
    }

    // Named type declarations (choice and compound types)
    for (const nt of file.namedTypes) {
        out.push(namedTypeSymbol(doc, nt));
    }

    for (const sec of file.sections) {
        const secChildren: DocumentSymbol[] = [
            ...sec.namedTypes.map(nt => namedTypeSymbol(doc, nt)),
            ...sec.options.map(o => optionSymbol(doc, o)),
        ];
        out.push({
            name: `section ${sec.title ? `"${sec.title}"` : "(unnamed)"}`,
            kind: SymbolKind.Package,
            range: toLspRange(doc, sec.range),
            selectionRange: toLspRange(doc, sec.titleRange ?? sec.range),
            children: secChildren,
        });
    }

    for (const o of file.options) {
        out.push(optionSymbol(doc, o));
    }

    for (const g of file.groups) {
        out.push({
            name: `group ${g.name}${g.mandatory ? " (mandatory)" : ""}`,
            kind: SymbolKind.Interface,
            range: toLspRange(doc, g.range),
            selectionRange: toLspRange(doc, g.nameRange),
            detail: g.options.map(o => o.name).join(", "),
        });
    }

    return out;
}

function namedTypeSymbol(doc: TextDocument, nt: NamedTypeDecl): DocumentSymbol {
    const isChoice = nt.typeExpr.kind === "ChoiceTypeExpr";
    return {
        name: nt.name,
        detail: isChoice
            ? `(${(nt.typeExpr as { members: { name: string }[] }).members.map(m => m.name).join(", ")})`
            : "{ … }",
        kind: isChoice ? SymbolKind.Enum : SymbolKind.Struct,
        range: toLspRange(doc, nt.range),
        selectionRange: toLspRange(doc, nt.nameRange),
        children: isChoice
            ? (nt.typeExpr as { members: { name: string; range: AstRange }[] }).members.map(m => ({
                name: m.name,
                kind: SymbolKind.EnumMember,
                range: toLspRange(doc, m.range),
                selectionRange: toLspRange(doc, m.range),
            }))
            : [],
    };
}

function optionSymbol(doc: TextDocument, o: OptionDecl): DocumentSymbol {
    const typeKv = o.qualifiers.find(q => q.key === "type");
    let detail = "";
    if (typeKv) {
        const v = typeKv.value;
        switch (v.kind) {
            case "TypeExpr":
                detail = v.baseName + (v.args.length
                    ? `(${v.args.map(a => a.name).join(", ")})`
                    : "");
                break;
            case "CompoundTypeExpr":
                detail = "{ … }";
                break;
            case "ChoiceTypeExpr":
                detail = `(${v.members.map(m => m.name).join(", ")})`;
                break;
            case "Identifier":
                detail = v.name;
                break;
            default:
                break;
        }
    }
    return {
        name: o.name,
        detail,
        kind: SymbolKind.Variable,
        range: toLspRange(doc, o.range),
        selectionRange: toLspRange(doc, o.nameRange),
    };
}

function toLspRange(doc: TextDocument, r: AstRange) {
    return { start: doc.positionAt(r.start), end: doc.positionAt(r.end) };
}
