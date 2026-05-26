/* ============================================================================
 *  hover.ts
 *  ----------------------------------------------------------------------------
 *  Hover provider: when the user hovers over a keyword, type, or qualifier,
 *  return a Markdown excerpt from the spec.
 * ========================================================================== */

import { Hover, MarkupKind, Position } from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { KEYWORD_DOCS } from "./keywords";

const IDENT_RE = /[A-Za-z_@][A-Za-z0-9_\-]*/;

export function buildHover(doc: TextDocument, position: Position): Hover | null {
    const text = doc.getText();
    const offset = doc.offsetAt(position);

    // Find the word boundaries around the cursor.
    let s = offset;
    while (s > 0 && /[A-Za-z0-9_\-@]/.test(text[s - 1])) s--;
    let e = offset;
    while (e < text.length && /[A-Za-z0-9_\-]/.test(text[e])) e++;

    if (s === e) return null;

    let word = text.slice(s, e);
    // Recover the leading @ for directive tokens.
    if (s > 0 && text[s - 1] === "@") {
        word = "@" + word;
        s--;
    }
    if (!IDENT_RE.test(word)) return null;

    const doc_ = KEYWORD_DOCS[word];
    if (!doc_) return null;

    return {
        contents: { kind: MarkupKind.Markdown, value: doc_ },
        range: { start: doc.positionAt(s), end: doc.positionAt(e) },
    };
}
