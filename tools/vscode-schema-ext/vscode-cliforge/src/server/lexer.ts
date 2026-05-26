/* ============================================================================
 *  lexer.ts
 *  ----------------------------------------------------------------------------
 *  Hand-rolled lexer for the cliforge schema language.
 *
 *  Produces a flat stream of typed tokens, each tagged with an inclusive
 *  start / exclusive end offset in the source text. The parser consumes this
 *  stream; the validator and providers can correlate AST nodes back to source
 *  spans through it.
 *
 *  Design notes
 *  ------------
 *  - The lexer never throws. Malformed input emits an `Invalid` token so the
 *    parser can still produce a partial AST (good for live editing).
 *  - Comments and doc-comments are preserved as tokens; the parser attaches
 *    doc comments to the next declaration.
 *  - Quantity literals (number+suffix, no space) are emitted as a *single*
 *    `Quantity` token to simplify range-expression parsing like `1ms..10s`.
 * ========================================================================== */

import { UNIT_SUFFIXES } from "./keywords";

export type TokenType =
    | "Identifier"
    | "Number"
    | "Quantity"
    | "String"
    | "Char"
    | "Directive"        // @schema, @import
    | "DocComment"       // ///, /** ... */, /*! ... */
    | "Comment"          // //, /* ... */
    | "LBrace" | "RBrace"
    | "LBracket" | "RBracket"
    | "LParen" | "RParen"
    | "Comma" | "Colon" | "Semicolon" | "Dot"
    | "Assign"           // =
    | "Eq"               // ==
    | "NotEq"            // !=
    | "Range"            // ..
    | "LogicalAnd"       // &&
    | "LogicalOr"        // ||
    | "Not"              // !
    | "EOF"
    | "Invalid";

export interface Token {
    type: TokenType;
    /** Source text for the token (incl. quotes/suffixes). */
    value: string;
    /** Inclusive start offset (0-based). */
    start: number;
    /** Exclusive end offset (0-based). */
    end: number;
    /** Error message when type === "Invalid". */
    message?: string;
}

const isLetter = (c: string): boolean =>
    (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c === "_";

const isDigit = (c: string): boolean => c >= "0" && c <= "9";

const isIdentStart = (c: string): boolean => isLetter(c);

const isIdentPart = (c: string): boolean =>
    isLetter(c) || isDigit(c) || c === "-";

const isHexDigit = (c: string): boolean =>
    isDigit(c) || (c >= "a" && c <= "f") || (c >= "A" && c <= "F");

export function tokenize(source: string): Token[] {
    const tokens: Token[] = [];
    const len = source.length;
    let pos = 0;

    const peek = (offset = 0): string =>
        pos + offset < len ? source[pos + offset] : "";

    const push = (type: TokenType, start: number, end: number, message?: string): void => {
        const tok: Token = { type, value: source.slice(start, end), start, end };
        if (message) tok.message = message;
        tokens.push(tok);
    };

    /* ----------------- scanning helpers ----------------- */

    const scanLineComment = (): void => {
        const start = pos;
        const isDoc = peek(2) === "/";   // "///" form
        // consume the entire line including the "//"
        while (pos < len && source[pos] !== "\n") pos++;
        push(isDoc ? "DocComment" : "Comment", start, pos);
    };

    const scanBlockComment = (): void => {
        const start = pos;
        const isDoc = peek(2) === "*" || peek(2) === "!";
        pos += 2;
        let closed = false;
        while (pos < len) {
            if (source[pos] === "*" && peek(1) === "/") {
                pos += 2;
                closed = true;
                break;
            }
            pos++;
        }
        if (!closed) {
            push("Invalid", start, len, "Unterminated block comment");
            return;
        }
        push(isDoc ? "DocComment" : "Comment", start, pos);
    };

    const scanString = (): void => {
        const start = pos;
        pos++;                   // opening quote
        while (pos < len) {
            const c = source[pos];
            if (c === "\\" && pos + 1 < len) {
                pos += 2;
                continue;
            }
            if (c === "\"") {
                pos++;
                push("String", start, pos);
                return;
            }
            if (c === "\n") {
                push("Invalid", start, pos, "Unterminated string literal");
                return;
            }
            pos++;
        }
        push("Invalid", start, pos, "Unterminated string literal");
    };

    const scanChar = (): void => {
        const start = pos;
        pos++;                   // opening quote
        if (pos < len && source[pos] === "\\" && pos + 1 < len) {
            pos += 2;
        } else if (pos < len) {
            pos++;
        }
        if (pos < len && source[pos] === "'") {
            pos++;
            push("Char", start, pos);
            return;
        }
        push("Invalid", start, pos, "Unterminated character literal");
    };

    const scanIdentifier = (): void => {
        const start = pos;
        while (pos < len && isIdentPart(source[pos])) pos++;
        push("Identifier", start, pos);
    };

    const scanDirective = (): void => {
        const start = pos;
        pos++;                   // consume '@'
        while (pos < len && isIdentPart(source[pos])) pos++;
        push("Directive", start, pos);
    };

    const scanNumberOrQuantity = (): void => {
        const start = pos;

        // optional leading sign already consumed by the caller (we handle bare
        // numbers here; signed quantities are parsed only after operators).
        if (source[pos] === "+" || source[pos] === "-") pos++;

        // hex / binary / octal prefixed literals
        if (source[pos] === "0" && pos + 1 < len) {
            const next = source[pos + 1];
            if (next === "x" || next === "X") {
                pos += 2;
                while (pos < len && (isHexDigit(source[pos]) || source[pos] === "_")) pos++;
                finishNumber(start);
                return;
            }
            if (next === "b" || next === "B") {
                pos += 2;
                while (pos < len && (source[pos] === "0" || source[pos] === "1" || source[pos] === "_")) pos++;
                finishNumber(start);
                return;
            }
            if (next === "o" || next === "O") {
                pos += 2;
                while (pos < len && (source[pos] >= "0" && source[pos] <= "7") || source[pos] === "_") pos++;
                finishNumber(start);
                return;
            }
        }

        while (pos < len && (isDigit(source[pos]) || source[pos] === "_")) pos++;

        // fractional part
        if (pos < len && source[pos] === "." && pos + 1 < len && isDigit(source[pos + 1])) {
            pos++;
            while (pos < len && (isDigit(source[pos]) || source[pos] === "_")) pos++;
        }

        // exponent
        if (pos < len && (source[pos] === "e" || source[pos] === "E")) {
            pos++;
            if (pos < len && (source[pos] === "+" || source[pos] === "-")) pos++;
            while (pos < len && isDigit(source[pos])) pos++;
        }

        finishNumber(start);
    };

    const finishNumber = (start: number): void => {
        // Quantity suffix: contiguous letters/percent, must be a known unit.
        const suffixStart = pos;
        if (pos < len && source[pos] === "%") {
            pos++;
        } else if (pos < len && isLetter(source[pos])) {
            // greedy letter run (units are short, max 3 chars among our set)
            while (pos < len && isLetter(source[pos])) pos++;
            const suffix = source.slice(suffixStart, pos);
            if (!UNIT_SUFFIXES.has(suffix)) {
                // Not a recognized unit — treat as separate identifier next round.
                pos = suffixStart;
                push("Number", start, pos);
                return;
            }
        } else if (pos < len && source[pos] === "µ") {
            // µs special case
            pos++;
            if (pos < len && source[pos] === "s") pos++;
            const suffix = source.slice(suffixStart, pos);
            if (!UNIT_SUFFIXES.has(suffix)) {
                pos = suffixStart;
                push("Number", start, pos);
                return;
            }
        }

        if (pos > suffixStart) {
            push("Quantity", start, pos);
        } else {
            push("Number", start, pos);
        }
    };

    /* ----------------- main loop ----------------- */

    while (pos < len) {
        const c = source[pos];

        // whitespace (incl. newlines) — skipped
        if (c === " " || c === "\t" || c === "\r" || c === "\n") {
            pos++;
            continue;
        }

        // comments
        if (c === "/" && peek(1) === "/") {
            scanLineComment();
            continue;
        }
        if (c === "/" && peek(1) === "*") {
            scanBlockComment();
            continue;
        }

        // directives
        if (c === "@") {
            scanDirective();
            continue;
        }

        // strings, chars
        if (c === "\"") { scanString(); continue; }
        if (c === "'")  { scanChar();   continue; }

        // punctuation / operators
        const start = pos;
        switch (c) {
            case "{": pos++; push("LBrace",    start, pos); continue;
            case "}": pos++; push("RBrace",    start, pos); continue;
            case "[": pos++; push("LBracket",  start, pos); continue;
            case "]": pos++; push("RBracket",  start, pos); continue;
            case "(": pos++; push("LParen",    start, pos); continue;
            case ")": pos++; push("RParen",    start, pos); continue;
            case ",": pos++; push("Comma",     start, pos); continue;
            case ":": pos++; push("Colon",     start, pos); continue;
            case ";": pos++; push("Semicolon", start, pos); continue;
            case "=":
                if (peek(1) === "=") { pos += 2; push("Eq",     start, pos); continue; }
                pos++; push("Assign", start, pos); continue;
            case ".":
                if (peek(1) === ".") { pos += 2; push("Range", start, pos); continue; }
                pos++; push("Dot", start, pos); continue;
            case "&":
                if (peek(1) === "&") { pos += 2; push("LogicalAnd", start, pos); continue; }
                pos++; push("Invalid", start, pos, "Unexpected '&'; did you mean '&&'?"); continue;
            case "|":
                if (peek(1) === "|") { pos += 2; push("LogicalOr",  start, pos); continue; }
                pos++; push("Invalid", start, pos, "Unexpected '|'; did you mean '||'?"); continue;
            case "!":
                if (peek(1) === "=") { pos += 2; push("NotEq",  start, pos); continue; }
                pos++; push("Not", start, pos); continue;
            default: break;
        }

        // identifiers / keywords
        if (isIdentStart(c)) {
            scanIdentifier();
            continue;
        }

        // numeric literals (possibly signed)
        if (isDigit(c) || ((c === "+" || c === "-") && isDigit(peek(1) ?? ""))) {
            scanNumberOrQuantity();
            continue;
        }

        // anything else — emit a single-character invalid token and advance.
        pos++;
        push("Invalid", start, pos, `Unexpected character '${c}'`);
    }

    tokens.push({ type: "EOF", value: "", start: len, end: len });
    return tokens;
}

/** Filter out trivia (comments & doc-comments). The parser uses this for the
 *  main token stream and a parallel doc-comment list for attaching docs. */
export function stripTrivia(tokens: Token[]): { code: Token[]; trivia: Token[] } {
    const code: Token[] = [];
    const trivia: Token[] = [];
    for (const t of tokens) {
        if (t.type === "Comment" || t.type === "DocComment") {
            trivia.push(t);
        } else {
            code.push(t);
        }
    }
    return { code, trivia };
}
