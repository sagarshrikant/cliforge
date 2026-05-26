/* ============================================================================
 *  definitions.ts
 *  ----------------------------------------------------------------------------
 *  Go-to-Definition:
 *    - @import "path/to/x.cf"           → opens the imported file
 *    - Type name that names a NamedTypeDecl → its declaration
 *    - Identifier inside group `options = [...]` → option declaration
 * ========================================================================== */

import { Location, Position } from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { SchemaFile, Range as AstRange } from "./ast";
import * as path from "path";
import * as fs from "fs";
import { URI } from "vscode-uri";

export function buildDefinition(
    doc: TextDocument,
    position: Position,
    file: SchemaFile,
): Location | Location[] | null {
    const offset = doc.offsetAt(position);

    // 1. Inside an @import path string → open the imported file
    for (const imp of file.imports) {
        if (withinRange(offset, imp.pathRange)) {
            const target = resolveImportPath(doc.uri, imp.path);
            if (target) {
                return Location.create(target, {
                    start: { line: 0, character: 0 },
                    end:   { line: 0, character: 0 },
                });
            }
            return null;
        }
    }

    // 2. Word under cursor
    const word = wordAt(doc, position);
    if (!word) return null;

    // Named type declaration lookup (replaces old enum lookup)
    for (const nt of file.namedTypes) {
        if (nt.name === word.text) {
            return Location.create(doc.uri, lspRange(doc, nt.nameRange));
        }
    }
    // Also check types declared inside sections
    for (const sec of file.sections) {
        for (const nt of sec.namedTypes) {
            if (nt.name === word.text) {
                return Location.create(doc.uri, lspRange(doc, nt.nameRange));
            }
        }
    }

    // Option lookup (for group `options = [...]` references)
    const allOptions = [
        ...file.options,
        ...file.sections.flatMap(s => s.options),
    ];
    for (const o of allOptions) {
        if (o.name === word.text) {
            return Location.create(doc.uri, lspRange(doc, o.nameRange));
        }
    }

    return null;
}

function withinRange(offset: number, r: AstRange): boolean {
    return offset >= r.start && offset <= r.end;
}

function lspRange(doc: TextDocument, r: AstRange) {
    return { start: doc.positionAt(r.start), end: doc.positionAt(r.end) };
}

function wordAt(doc: TextDocument, position: Position): { text: string } | null {
    const text = doc.getText();
    const offset = doc.offsetAt(position);
    let s = offset;
    while (s > 0 && /[A-Za-z0-9_\-]/.test(text[s - 1])) s--;
    let e = offset;
    while (e < text.length && /[A-Za-z0-9_\-]/.test(text[e])) e++;
    if (s === e) return null;
    return { text: text.slice(s, e) };
}

function resolveImportPath(docUri: string, importPath: string): string | null {
    try {
        const baseDir = path.dirname(URI.parse(docUri).fsPath);
        const candidate = path.resolve(baseDir, importPath);
        if (fs.existsSync(candidate)) {
            return URI.file(candidate).toString();
        }
    } catch {
        return null;
    }
    return null;
}
