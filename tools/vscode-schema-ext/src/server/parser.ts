/* ============================================================================
 *  parser.ts
 *  ----------------------------------------------------------------------------
 *  Recursive-descent parser for cliforge schemas.
 *
 *  Design: forgiving. On any error the parser records a diagnostic and tries
 *  to resync at the next top-level brace boundary so the editor still gets a
 *  workable AST. Doc comments collected by the lexer are attached to the
 *  *next* declaration the parser produces.
 * ========================================================================== */

import {
    SchemaFile, SchemaDirective, ImportDirective, MetaBlock, SectionBlock,
    OptionDecl, GroupDecl, SubcommandDecl, PositionalDecl,
    NamedTypeDecl, ConditionalBlock,
    KeyValue, ValueExpr, TypeExpr, TypeArg,
    CompoundTypeExpr, CompoundField, ChoiceTypeExpr,
    VariantDefaultBlock, VariantArm, AllowedBlock, AllowedArm,
    IdentifierList, RangeExpr, MultipleExpr, Identifier, Literal, Range,
    Node,
} from "./ast";
import { Token, tokenize, stripTrivia } from "./lexer";
import {
    BLOCK_KEYWORDS, PRIMITIVE_TYPES, QUANTITY_TYPES, CONDITIONAL_KEYWORDS,
} from "./keywords";

export interface ParseDiagnostic {
    range: Range;
    message: string;
    severity: "error" | "warning";
    code?: string;
}

export interface ParseResult {
    file: SchemaFile;
    diagnostics: ParseDiagnostic[];
    tokens: Token[];
}

export function parse(source: string): ParseResult {
    const allTokens = tokenize(source);
    const { code, trivia } = stripTrivia(allTokens);
    const state = new ParserState(source, code, trivia);
    const file = state.parseFile();
    return { file, diagnostics: state.diagnostics, tokens: allTokens };
}

/* ---------------------------------------------------------------------------- */

class ParserState {
    private idx = 0;
    public diagnostics: ParseDiagnostic[] = [];

    constructor(
        private readonly source: string,
        private readonly tokens: Token[],
        private readonly trivia: Token[],
    ) {}

    /* --------- helpers ---------- */

    private peek(offset = 0): Token {
        return this.tokens[Math.min(this.idx + offset, this.tokens.length - 1)];
    }

    private advance(): Token {
        const t = this.tokens[this.idx];
        if (this.idx < this.tokens.length - 1) this.idx++;
        return t;
    }

    private match(type: Token["type"], value?: string): boolean {
        const t = this.peek();
        if (t.type !== type) return false;
        if (value !== undefined && t.value !== value) return false;
        return true;
    }

    private consume(type: Token["type"], message: string, value?: string): Token | null {
        if (this.match(type, value)) return this.advance();
        const t = this.peek();
        this.error(t.start, t.end, message);
        return null;
    }

    private error(start: number, end: number, message: string, code?: string): void {
        this.diagnostics.push({ range: { start, end }, message, severity: "error", code });
    }

    private warn(start: number, end: number, message: string, code?: string): void {
        this.diagnostics.push({ range: { start, end }, message, severity: "warning", code });
    }

    private takeDocBefore(start: number): string | undefined {
        for (let i = this.trivia.length - 1; i >= 0; i--) {
            const t = this.trivia[i];
            if (t.end > start) continue;
            if (t.type !== "DocComment") return undefined;
            const between = this.source.slice(t.end, start);
            if (/\S/.test(between)) return undefined;
            return t.value;
        }
        return undefined;
    }

    /* -------------------------------------------------------------------------
     * lookAheadConditionalHasBlock
     * -----------------------------------------------------------------------
     * Peeks forward from the current token (which must be a conditional keyword)
     * past the condition expression and returns true iff the next token is '{'.
     *
     * This distinguishes:
     *   • Inline guard (no block):  ifdef LINUX_BUILD          ← section / option qualifier
     *   • Full block:               ifdef EMBED_CRYPTO { ... } ← nested conditional
     * ---------------------------------------------------------------------- */
    private lookAheadConditionalHasBlock(): boolean {
        const kw = this.peek(0).value;
        let i = 1; // skip the keyword itself

        if (kw === "ifdef" || kw === "ifndef") {
            // Condition expression: one or more identifiers joined by ||, &&, !
            while (i < 30) {
                const t = this.peek(i);
                if (t.type === "Identifier" ||
                    t.value === "||" || t.value === "&&" || t.value === "!") {
                    i++;
                } else {
                    break;
                }
            }
        } else {
            // ifkey / ifnkey: KEY [op VALUE]
            i++; // skip KEY identifier
            const op = this.peek(i);
            if (op.value === "==" || op.value === "!=") {
                i++; // skip op
                i++; // skip VALUE
            }
        }

        return this.peek(i).type === "LBrace";
    }

    /* -------------------------------------------------------------------------
     * consumeInlineConditionalGuard
     * -----------------------------------------------------------------------
     * Consumes the conditional keyword and its condition expression without
     * expecting or consuming a '{...}' block.  Used for:
     *   • Section-level inline guards:  ifdef LINUX_BUILD  (inside section body)
     *   • Option-level inline guards:   ifkey dev_mode     (inside option body)
     *   • @import inline guards:        ifnkey no-stats    (handled in parseImport)
     * ---------------------------------------------------------------------- */
    private consumeInlineConditionalGuard(): void {
        const kwTok = this.advance(); // consume keyword (ifdef/ifndef/ifkey/ifnkey)

        if (kwTok.value === "ifdef" || kwTok.value === "ifndef") {
            // Consume identifiers and boolean operators
            while (!this.match("LBrace") && this.peek().type !== "EOF") {
                const t = this.peek();
                if (t.type === "Identifier" ||
                    t.value === "||" || t.value === "&&" || t.value === "!") {
                    this.advance();
                } else {
                    break;
                }
            }
        } else {
            // ifkey / ifnkey: KEY [op VALUE]
            if (this.peek().type === "Identifier") {
                this.advance(); // KEY
                if (this.match("Eq") || this.match("NotEq")) {
                    this.advance(); // op
                    if (this.peek().type === "Identifier") {
                        this.advance(); // VALUE
                    }
                }
            }
        }
    }

    /* --------- top-level ---------- */

    parseFile(): SchemaFile {
        const file: SchemaFile = {
            kind: "SchemaFile",
            range: { start: 0, end: this.source.length },
            imports: [],
            namedTypes: [],
            sections: [],
            options: [],
            groups: [],
            subcommands: [],
            conditionals: [],
            children: [],
        };

        if (this.match("Directive", "@schema")) {
            file.directive = this.parseSchemaDirective();
            file.children.push(file.directive);
        }

        while (this.peek().type !== "EOF") {
            const t = this.peek();
            try {
                if (t.type === "Directive" && t.value === "@import") {
                    const imp = this.parseImport();
                    file.imports.push(imp);
                    file.children.push(imp);
                } else if (t.type === "Directive" && t.value === "@schema") {
                    this.error(t.start, t.end, "Duplicate @schema directive", "duplicate-schema");
                    this.advance();
                } else if (t.type === "Identifier" && t.value === "meta") {
                    const m = this.parseMeta();
                    if (file.meta) {
                        this.error(m.range.start, m.range.start + 4, "Duplicate `meta` block", "duplicate-meta");
                    } else {
                        file.meta = m;
                    }
                    file.children.push(m);
                } else if (t.type === "Identifier" && t.value === "section") {
                    const s = this.parseSection();
                    file.sections.push(s);
                    file.children.push(s);
                } else if (t.type === "Identifier" && t.value === "option") {
                    const o = this.parseOption();
                    file.options.push(o);
                    file.children.push(o);
                } else if (t.type === "Identifier" && t.value === "group") {
                    const g = this.parseGroup();
                    file.groups.push(g);
                    file.children.push(g);
                } else if (t.type === "Identifier" && t.value === "subcommand") {
                    const sc = this.parseSubcommand();
                    file.subcommands.push(sc);
                    file.children.push(sc);
                } else if (t.type === "Identifier" && CONDITIONAL_KEYWORDS.has(t.value)) {
                    const cb = this.parseConditionalBlock();
                    file.conditionals.push(cb);
                    file.children.push(cb);
                } else if (t.type === "Identifier" && this.isNamedTypeDecl()) {
                    // name = (...)  or  name = { ... }
                    const nt = this.parseNamedTypeDecl();
                    file.namedTypes.push(nt);
                    file.children.push(nt);
                } else {
                    this.error(t.start, t.end,
                        `Unexpected token '${t.value || t.type}' at top level`,
                        "unexpected-token");
                    this.advance();
                }
            } catch {
                this.resyncToTopLevel();
            }
        }
        return file;
    }

    /** Look-ahead: is the next sequence `IDENT = ( ...` or `IDENT = { ...`? */
    private isNamedTypeDecl(): boolean {
        // peek(0) = Identifier, peek(1) = Assign, peek(2) = LParen or LBrace
        const p1 = this.peek(1);
        const p2 = this.peek(2);
        return p1.type === "Assign" &&
            (p2.type === "LParen" || p2.type === "LBrace");
    }

    /* --------- directives ---------- */

    private parseSchemaDirective(): SchemaDirective {
        const start = this.advance().start;       // consume @schema
        const langTok = this.consume("Identifier", "Expected 'cliforge' after @schema");
        const verTok  = this.peek();
        let version = "";
        if (verTok.type === "Identifier" && /^v\d+$/.test(verTok.value)) {
            version = verTok.value;
            this.advance();
        } else if (verTok.type === "Number") {
            version = "v" + verTok.value;
            this.advance();
        } else {
            this.error(verTok.start, verTok.end,
                "Expected schema version (e.g. 'v1') after 'cliforge'",
                "missing-schema-version");
        }
        const end = verTok.end;
        return {
            kind: "SchemaDirective",
            range: { start, end },
            language: langTok?.value ?? "",
            version,
        };
    }

    private parseImport(): ImportDirective {
        const startTok = this.advance();          // consume @import
        const pathTok = this.consume("String", "@import requires a quoted file path");
        let path = "";
        let pathRange: Range = { start: startTok.end, end: startTok.end };
        if (pathTok) {
            path = pathTok.value.slice(1, -1);
            pathRange = { start: pathTok.start, end: pathTok.end };
        }

        let alias: string | null = null;
        let aliasRange: Range | undefined;
        let ifkeyGuard: ImportDirective["ifkey"];
        let ifkeyRange: Range | undefined;

        // `as <alias>`
        if (this.match("Identifier", "as")) {
            this.advance();
            const aliasTok = this.consume("Identifier", "Expected alias identifier after 'as'");
            if (aliasTok) {
                alias = aliasTok.value;
                aliasRange = { start: aliasTok.start, end: aliasTok.end };
            }
        } else {
            this.error(pathTok?.end ?? startTok.end, pathTok?.end ?? startTok.end,
                "@import requires a mandatory 'as <alias>' clause",
                "missing-import-alias");
        }

        // optional `ifkey KEY`, `ifnkey KEY`, `ifdef SYMBOL`, `ifndef SYMBOL`
        const guardKwTok = this.peek();
        if (guardKwTok.type === "Identifier" && CONDITIONAL_KEYWORDS.has(guardKwTok.value)) {
            const kw = guardKwTok.value;
            const guardStart = this.advance().start; // consume ifkey/ifnkey/ifdef/ifndef

            if (kw === "ifdef" || kw === "ifndef") {
                // Collect symbol expression
                const parts: string[] = [];
                while (this.peek().type === "Identifier" ||
                       this.peek().value === "||" || this.peek().value === "&&") {
                    parts.push(this.advance().value);
                }
                const symbolStr = parts.join(" ");
                const guardEnd = this.peek(-1)?.end ?? guardStart;
                ifkeyGuard = { key: symbolStr, negated: kw === "ifndef" };
                ifkeyRange = { start: guardStart, end: guardEnd };
            } else {
                // ifkey / ifnkey: KEY [op VALUE]
                const keyTok = this.consume("Identifier", `Expected key name after '${kw}'`);
                if (keyTok) {
                    ifkeyGuard = { key: keyTok.value, negated: kw === "ifnkey" };
                    let guardEnd = keyTok.end;
                    if (this.match("Eq") || this.match("NotEq")) {
                        const opTok = this.advance();
                        const valTok = this.consume("Identifier", "Expected value after operator");
                        if (valTok) {
                            ifkeyGuard.op = opTok.value as "==" | "!=";
                            ifkeyGuard.value = valTok.value;
                            guardEnd = valTok.end;
                        }
                    }
                    ifkeyRange = { start: guardStart, end: guardEnd };
                }
            }
        }

        const doc = this.takeDocBefore(startTok.start);
        const end = (ifkeyRange ?? aliasRange ?? pathRange).end;
        return {
            kind: "ImportDirective",
            range: { start: startTok.start, end },
            path, pathRange,
            alias, aliasRange,
            ifkey: ifkeyGuard,
            ifkeyRange,
            leadingDoc: doc,
        };
    }

    /* --------- meta ---------- */

    private parseMeta(): MetaBlock {
        const startTok = this.advance();          // consume 'meta'
        this.consume("LBrace", "Expected '{' to start meta block");
        const entries: KeyValue[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const kv = this.parseKeyValue();
            if (kv) entries.push(kv);
        }
        const endTok = this.consume("RBrace", "Expected '}' to close meta block");
        const doc = this.takeDocBefore(startTok.start);
        return {
            kind: "MetaBlock",
            range: { start: startTok.start, end: endTok?.end ?? startTok.end },
            entries,
            leadingDoc: doc,
        };
    }

    /* --------- section ---------- */

    private parseSection(): SectionBlock {
        const startTok = this.advance();          // consume 'section'
        let title: string | undefined;
        let titleRange: Range | undefined;
        if (this.match("String")) {
            const t = this.advance();
            title = t.value.slice(1, -1);
            titleRange = { start: t.start, end: t.end };
        }
        this.consume("LBrace", "Expected '{' to start section block");
        const options: OptionDecl[] = [];
        const namedTypes: NamedTypeDecl[] = [];
        const conditionals: ConditionalBlock[] = [];
        const children: Node[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const t = this.peek();
            if (t.type === "Identifier" && t.value === "option") {
                const o = this.parseOption();
                options.push(o);
                children.push(o);
            } else if (t.type === "Identifier" && CONDITIONAL_KEYWORDS.has(t.value)) {
                // Could be an inline section guard (e.g. `ifdef LINUX_BUILD`)
                // OR a nested conditional block (e.g. `ifdef EMBED_CRYPTO { ... }`).
                // Peek ahead: if '{' follows the condition expression it is a block.
                if (this.lookAheadConditionalHasBlock()) {
                    const cb = this.parseConditionalBlock();
                    conditionals.push(cb);
                    children.push(cb);
                } else {
                    // Inline guard — consume the condition, store nothing
                    this.consumeInlineConditionalGuard();
                }
            } else if (t.type === "Identifier" && this.isNamedTypeDecl()) {
                const nt = this.parseNamedTypeDecl();
                namedTypes.push(nt);
                children.push(nt);
            } else if (t.type === "Identifier" && t.value === "description") {
                // description = "..." as a section-level qualifier — swallow it
                this.advance();
                this.consume("Assign", "Expected '=' after 'description'");
                this.parseValueExpr("description");
            } else {
                this.error(t.start, t.end,
                    `Unexpected token '${t.value || t.type}' inside section`,
                    "unexpected-in-section");
                this.advance();
            }
        }
        const endTok = this.consume("RBrace", "Expected '}' to close section block");
        const doc = this.takeDocBefore(startTok.start);
        return {
            kind: "SectionBlock",
            range: { start: startTok.start, end: endTok?.end ?? startTok.end },
            title, titleRange,
            options, namedTypes, conditionals, children,
            leadingDoc: doc,
        };
    }

    /* --------- option ---------- */

    private parseOption(): OptionDecl {
        const startTok = this.advance();          // consume 'option'
        const nameTok = this.consume("Identifier", "Expected option name");
        const name = nameTok?.value ?? "";
        const nameRange: Range = nameTok
            ? { start: nameTok.start, end: nameTok.end }
            : { start: startTok.end, end: startTok.end };
        this.consume("LBrace", "Expected '{' to start option block");
        const qualifiers: KeyValue[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const t = this.peek();
            // `allowed { }` — parse as a special block, not a key-value
            if (t.type === "Identifier" && t.value === "allowed") {
                const ab = this.parseAllowedBlock();
                qualifiers.push({
                    kind: "KeyValue",
                    range: ab.range,
                    key: "allowed",
                    keyRange: { start: t.start, end: t.end },
                    value: ab,
                });
                continue;
            }
            // Inline conditional qualifier: `ifdef SYMBOL` or `ifkey KEY` (no '=')
            if (t.type === "Identifier" && CONDITIONAL_KEYWORDS.has(t.value)) {
                const kv = this.parseInlineConditionalQualifier();
                if (kv) qualifiers.push(kv);
                continue;
            }
            const kv = this.parseKeyValue();
            if (kv) qualifiers.push(kv);
        }
        const endTok = this.consume("RBrace", "Expected '}' to close option block");
        const doc = this.takeDocBefore(startTok.start);
        return {
            kind: "OptionDecl",
            range: { start: startTok.start, end: endTok?.end ?? startTok.end },
            name, nameRange,
            qualifiers,
            leadingDoc: doc,
        };
    }

    /* -------------------------------------------------------------------------
     * parseInlineConditionalQualifier
     * -----------------------------------------------------------------------
     * Handles option-level bare conditional qualifiers such as:
     *   ifdef    LINUX_BUILD        (option qualifier, no '=')
     *   ifkey    dev_mode           (option qualifier, no '=')
     *   ifnkey   release            (option qualifier, no '=')
     *
     * Produces a synthetic KeyValue so the AST round-trips cleanly and the
     * validator can skip it.  The value is stored as a string literal whose
     * raw text is the condition expression (e.g. "LINUX_BUILD").
     * ---------------------------------------------------------------------- */
    private parseInlineConditionalQualifier(): KeyValue | null {
        const kwTok = this.advance(); // consume ifdef/ifndef/ifkey/ifnkey
        const parts: string[] = [];

        if (kwTok.value === "ifdef" || kwTok.value === "ifndef") {
            while (!this.match("RBrace") && this.peek().type !== "EOF") {
                const t = this.peek();
                if (t.type === "Identifier" ||
                    t.value === "||" || t.value === "&&" || t.value === "!") {
                    parts.push(t.value);
                    this.advance();
                } else {
                    break;
                }
            }
        } else {
            // ifkey / ifnkey: KEY [op VALUE]
            if (this.peek().type === "Identifier") {
                parts.push(this.advance().value); // KEY
                if (this.match("Eq") || this.match("NotEq")) {
                    const opTok = this.advance();
                    parts.push(opTok.value);
                    if (this.peek().type === "Identifier") {
                        parts.push(this.advance().value); // VALUE
                    }
                }
            }
        }

        const expr = parts.join(" ");
        const exprEnd = kwTok.end + (expr ? expr.length + 1 : 0);
        const valueNode: Literal = {
            kind: "Literal",
            range: { start: kwTok.end, end: exprEnd },
            raw: expr,
            literalKind: "string",
            stringValue: expr,
        };
        return {
            kind: "KeyValue",
            range: { start: kwTok.start, end: exprEnd },
            key: kwTok.value,
            keyRange: { start: kwTok.start, end: kwTok.end },
            value: valueNode,
        };
    }

    /* --------- group ---------- */

    private parseGroup(): GroupDecl {
        const startTok = this.advance();          // 'group'
        const nameTok = this.consume("Identifier", "Expected group name");
        const name = nameTok?.value ?? "";
        const nameRange: Range = nameTok
            ? { start: nameTok.start, end: nameTok.end }
            : { start: startTok.end, end: startTok.end };
        this.consume("LBrace", "Expected '{' to start group block");
        const options: Identifier[] = [];
        let mandatory = false;
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const t = this.peek();
            if (t.type === "Identifier" && t.value === "mandatory") {
                mandatory = true;
                this.advance();
                if (this.match("Semicolon")) this.advance();
            } else if (t.type === "Identifier" && t.value === "options") {
                this.advance();
                this.consume("Assign", "Expected '=' after 'options'");
                const list = this.parseIdentifierList();
                for (const id of list.items) options.push(id);
            } else {
                this.error(t.start, t.end,
                    `Unexpected token '${t.value || t.type}' in group block`,
                    "unexpected-in-group");
                this.advance();
            }
        }
        const endTok = this.consume("RBrace", "Expected '}' to close group block");
        const doc = this.takeDocBefore(startTok.start);
        return {
            kind: "GroupDecl",
            range: { start: startTok.start, end: endTok?.end ?? startTok.end },
            name, nameRange,
            options, mandatory,
            leadingDoc: doc,
        };
    }

    /* --------- subcommand ---------- */

    private parseSubcommand(): SubcommandDecl {
        const startTok = this.advance();          // consume 'subcommand'
        const nameTok = this.consume("Identifier", "Expected subcommand name");
        const name = nameTok?.value ?? "";
        const nameRange: Range = nameTok
            ? { start: nameTok.start, end: nameTok.end }
            : { start: startTok.end, end: startTok.end };
        this.consume("LBrace", "Expected '{' to start subcommand block");

        const options: OptionDecl[] = [];
        const positionals: PositionalDecl[] = [];
        const conditionals: ConditionalBlock[] = [];
        const children: Node[] = [];

        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const t = this.peek();
            if (t.type === "Identifier" && t.value === "option") {
                const o = this.parseOption();
                options.push(o);
                children.push(o);
            } else if (t.type === "Identifier" && t.value === "positional") {
                const p = this.parsePositional();
                positionals.push(p);
                children.push(p);
            } else if (t.type === "Identifier" && CONDITIONAL_KEYWORDS.has(t.value)) {
                if (this.lookAheadConditionalHasBlock()) {
                    const cb = this.parseConditionalBlock();
                    conditionals.push(cb);
                    children.push(cb);
                } else {
                    this.consumeInlineConditionalGuard();
                }
            } else if (t.type === "Identifier" &&
                       (t.value === "brief" || t.value === "description" ||
                        t.value === "deprecated")) {
                // Key-value metadata for the subcommand itself
                this.advance();
                this.consume("Assign", `Expected '=' after '${t.value}'`);
                this.parseValueExpr(t.value);
            } else {
                this.error(t.start, t.end,
                    `Unexpected token '${t.value || t.type}' inside subcommand`,
                    "unexpected-in-subcommand");
                this.advance();
            }
        }
        const endTok = this.consume("RBrace", "Expected '}' to close subcommand block");
        const doc = this.takeDocBefore(startTok.start);
        return {
            kind: "SubcommandDecl",
            range: { start: startTok.start, end: endTok?.end ?? startTok.end },
            name, nameRange,
            options, positionals, conditionals, children,
            leadingDoc: doc,
        };
    }

    /* --------- positional ---------- */

    private parsePositional(): PositionalDecl {
        const startTok = this.advance();          // consume 'positional'
        const nameTok = this.consume("Identifier", "Expected positional argument name");
        const name = nameTok?.value ?? "";
        const nameRange: Range = nameTok
            ? { start: nameTok.start, end: nameTok.end }
            : { start: startTok.end, end: startTok.end };
        this.consume("LBrace", "Expected '{' to start positional block");
        const qualifiers: KeyValue[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const kv = this.parseKeyValue();
            if (kv) qualifiers.push(kv);
        }
        const endTok = this.consume("RBrace", "Expected '}' to close positional block");
        const doc = this.takeDocBefore(startTok.start);
        return {
            kind: "PositionalDecl",
            range: { start: startTok.start, end: endTok?.end ?? startTok.end },
            name, nameRange,
            qualifiers,
            leadingDoc: doc,
        };
    }

    /* --------- named type declaration ---------- */
    /* Covers:  name = (A, B, C)    and    name = { field: type, ... }   */

    private parseNamedTypeDecl(): NamedTypeDecl {
        const nameTok = this.advance();           // identifier
        const nameRange: Range = { start: nameTok.start, end: nameTok.end };
        this.consume("Assign", "Expected '=' after type name");

        let typeExpr: ChoiceTypeExpr | CompoundTypeExpr;
        if (this.match("LParen")) {
            typeExpr = this.parseChoiceType();
        } else {
            typeExpr = this.parseCompoundType();
        }
        return {
            kind: "NamedTypeDecl",
            range: { start: nameTok.start, end: typeExpr.range.end },
            name: nameTok.value,
            nameRange,
            typeExpr,
        };
    }

    /* --------- conditional block ---------- */

    private parseConditionalBlock(): ConditionalBlock {
        const kwTok = this.advance();             // ifdef / ifndef / ifkey / ifnkey
        const node: ConditionalBlock = {
            kind: "ConditionalBlock",
            range: { start: kwTok.start, end: kwTok.end },
            keyword: kwTok.value,
            children: [],
        };

        if (kwTok.value === "ifdef" || kwTok.value === "ifndef") {
            // Collect symbol expression: SYMBOL [|| SYMBOL ...]
            const parts: string[] = [];
            while (!this.match("LBrace") && this.peek().type !== "EOF") {
                const t = this.peek();
                if (t.type === "Identifier" || t.value === "||" || t.value === "&&") {
                    parts.push(t.value);
                    this.advance();
                } else {
                    break;
                }
            }
            node.symbolExpr = parts.join(" ");
        } else {
            // ifkey / ifnkey: KEY [== VALUE]
            const keyTok = this.consume("Identifier", `Expected key name after '${kwTok.value}'`);
            if (keyTok) {
                node.keyName = keyTok.value;
                if (this.match("Eq") || this.match("NotEq")) {
                    const opTok = this.advance();
                    const valTok = this.consume("Identifier", "Expected value after operator");
                    if (valTok) {
                        node.op = opTok.value as "==" | "!=";
                        node.keyValue = valTok.value;
                    }
                }
            }
        }

        this.consume("LBrace", `Expected '{' to start ${kwTok.value} block`);
        const children: Node[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const t = this.peek();
            if (t.type === "Identifier" && t.value === "section") {
                children.push(this.parseSection());
            } else if (t.type === "Identifier" && t.value === "option") {
                children.push(this.parseOption());
            } else if (t.type === "Identifier" && t.value === "group") {
                children.push(this.parseGroup());
            } else if (t.type === "Identifier" && t.value === "subcommand") {
                children.push(this.parseSubcommand());
            } else if (t.type === "Identifier" && CONDITIONAL_KEYWORDS.has(t.value)) {
                children.push(this.parseConditionalBlock());
            } else if (t.type === "Identifier" && this.isNamedTypeDecl()) {
                children.push(this.parseNamedTypeDecl());
            } else {
                this.error(t.start, t.end,
                    `Unexpected token '${t.value || t.type}' inside ${kwTok.value} block`,
                    "unexpected-in-conditional");
                this.advance();
            }
        }
        const endTok = this.consume("RBrace", `Expected '}' to close ${kwTok.value} block`);
        node.children = children;
        node.range = { start: kwTok.start, end: endTok?.end ?? kwTok.end };
        return node;
    }

    /* --------- type shapes ---------- */

    private parseChoiceType(): ChoiceTypeExpr {
        const open = this.advance();              // '('
        const members: Identifier[] = [];
        while (!this.match("RParen") && this.peek().type !== "EOF") {
            if (this.match("Comma")) { this.advance(); continue; }
            const t = this.peek();
            if (t.type === "Identifier") {
                this.advance();
                members.push({
                    kind: "Identifier",
                    range: { start: t.start, end: t.end },
                    name: t.value,
                });
            } else {
                this.error(t.start, t.end,
                    `Expected member name in choice type, got '${t.value || t.type}'`,
                    "bad-choice-member");
                this.advance();
            }
        }
        const close = this.consume("RParen", "Expected ')' to close choice type");
        return {
            kind: "ChoiceTypeExpr",
            range: { start: open.start, end: close?.end ?? open.end },
            members,
        };
    }

    private parseCompoundType(): CompoundTypeExpr {
        const open = this.advance();              // '{'
        const fields: CompoundField[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const f = this.parseCompoundField();
            if (f) fields.push(f);
        }
        const close = this.consume("RBrace", "Expected '}' to close compound type");
        return {
            kind: "CompoundTypeExpr",
            range: { start: open.start, end: close?.end ?? open.end },
            fields,
        };
    }

    private parseCompoundField(): CompoundField | null {
        const nameTok = this.peek();
        if (nameTok.type !== "Identifier") {
            this.error(nameTok.start, nameTok.end,
                `Expected field name, got '${nameTok.value || nameTok.type}'`,
                "expected-field-name");
            this.advance();
            return null;
        }
        this.advance();
        this.consume("Colon", `Expected ':' after field name '${nameTok.value}'`);
        const typeExpr = this.parseTypeExpr();
        let defaultValue: ValueExpr | undefined;
        if (this.match("Assign")) {
            this.advance();
            defaultValue = this.parseValueExpr("default");
        }
        if (this.match("Comma")) this.advance();
        if (this.match("Semicolon")) this.advance();
        const doc = this.takeDocBefore(nameTok.start);
        const endRange = defaultValue?.range.end ?? typeExpr.range.end;
        const field: CompoundField = {
            kind: "CompoundField",
            range: { start: nameTok.start, end: endRange },
            name: nameTok.value,
            nameRange: { start: nameTok.start, end: nameTok.end },
            fieldType: typeExpr.kind === "TypeExpr"
                ? typeExpr
                : {
                    kind: "TypeExpr",
                    range: typeExpr.range,
                    baseName: "compound",
                    baseRange: typeExpr.range,
                    args: [],
                },
            defaultValue,
            fieldDoc: doc,
        };
        if (typeExpr.kind === "TypeExpr" && typeExpr.rangeConstraint) {
            field.rangeConstraint = typeExpr.rangeConstraint;
        }
        return field;
    }

    /* --------- allowed block ---------- */

    private parseAllowedBlock(): AllowedBlock {
        const startTok = this.advance();          // consume 'allowed'
        this.consume("LBrace", "Expected '{' to start allowed block");
        const arms: AllowedArm[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const condTok = this.peek();
            if (condTok.type !== "Identifier") {
                this.error(condTok.start, condTok.end, "Expected condition keyword or 'default'");
                this.advance();
                continue;
            }
            this.advance();
            let condName: string | undefined;
            let op: "==" | "!=" | undefined;
            let condValue: string | undefined;
            if (condTok.value !== "default") {
                const nameTok = this.consume("Identifier", `Expected name after '${condTok.value}'`);
                if (nameTok) condName = nameTok.value;
                if (this.match("Eq") || this.match("NotEq")) {
                    const opTok = this.advance();
                    const valTok = this.consume("Identifier", "Expected value");
                    if (valTok) { op = opTok.value as "==" | "!="; condValue = valTok.value; }
                }
            }
            this.consume("Colon", "Expected ':' after condition");
            // parse (A, B, C) member list
            const members: Identifier[] = [];
            if (this.match("LParen")) {
                this.advance();
                while (!this.match("RParen") && this.peek().type !== "EOF") {
                    if (this.match("Comma")) { this.advance(); continue; }
                    const m = this.peek();
                    if (m.type === "Identifier") {
                        this.advance();
                        members.push({ kind: "Identifier", range: { start: m.start, end: m.end }, name: m.value });
                    } else { this.advance(); }
                }
                this.consume("RParen", "Expected ')' to close allowed value list");
            }
            if (this.match("Comma")) this.advance();
            arms.push({
                kind: "AllowedArm",
                range: { start: condTok.start, end: members[members.length - 1]?.range.end ?? condTok.end },
                condKind: condTok.value,
                condName, op, condValue,
                values: members,
            });
        }
        const endTok = this.consume("RBrace", "Expected '}' to close allowed block");
        return {
            kind: "AllowedBlock",
            range: { start: startTok.start, end: endTok?.end ?? startTok.end },
            arms,
        };
    }

    /* --------- key = value ---------- */

    private parseKeyValue(): KeyValue | null {
        const keyTok = this.peek();
        if (keyTok.type !== "Identifier") {
            this.error(keyTok.start, keyTok.end,
                `Expected qualifier name, got '${keyTok.value || keyTok.type}'`,
                "expected-qualifier");
            this.advance();
            return null;
        }
        this.advance();
        const keyRange: Range = { start: keyTok.start, end: keyTok.end };
        this.consume("Assign", `Expected '=' after qualifier '${keyTok.value}'`);
        const value = this.parseValueExpr(keyTok.value);
        const valueEnd = value?.range.end ?? keyTok.end;
        if (this.match("Semicolon")) this.advance();
        const doc = this.takeDocBefore(keyTok.start);
        return {
            kind: "KeyValue",
            range: { start: keyTok.start, end: valueEnd },
            key: keyTok.value,
            keyRange,
            value: value!,
            leadingDoc: doc,
        };
    }

    /* --------- value expressions ---------- */

    private parseValueExpr(contextKey: string): ValueExpr {
        const t = this.peek();

        // Variant-keyed default block:  default = { ifdef ... }
        if (contextKey === "default" && t.type === "LBrace") {
            return this.parseVariantDefaultBlock();
        }

        // Identifier list:  options = [ a, b, c ]
        if (t.type === "LBracket") {
            return this.parseIdentifierList();
        }

        // `multiple = N` or `multiple = N..M`
        if (contextKey === "multiple" && t.type === "Number") {
            return this.parseMultiple();
        }

        // Inline choice type:  type = (A, B, C)
        if (contextKey === "type" && t.type === "LParen") {
            return this.parseChoiceType();
        }

        // Inline compound type:  type = { field: type, ... }
        if (contextKey === "type" && t.type === "LBrace") {
            return this.parseCompoundType();
        }

        // Boolean or string literal
        if (t.type === "String" || t.type === "Char" || t.type === "Number" ||
            t.type === "Quantity" ||
            (t.type === "Identifier" && (t.value === "true" || t.value === "false"))) {
            const lit = this.parseLiteral();
            if (this.match("Range") &&
                (lit.literalKind === "number" || lit.literalKind === "quantity")) {
                this.advance();
                const highTok = this.peek();
                if (highTok.type === "Number" || highTok.type === "Quantity") {
                    const high = this.parseLiteral();
                    return {
                        kind: "RangeExpr",
                        range: { start: lit.range.start, end: high.range.end },
                        low: lit, high,
                    } as RangeExpr;
                }
                this.error(highTok.start, highTok.end, "Expected upper bound after '..'", "expected-bound");
            }
            return lit;
        }

        // Type expression
        if (t.type === "Identifier") {
            if (contextKey === "type" || isTypeStart(t.value)) {
                return this.parseTypeExpr();
            }
            const id = this.advance();
            return { kind: "Identifier", range: { start: id.start, end: id.end }, name: id.value } as Identifier;
        }

        this.error(t.start, t.end, `Expected value expression, got '${t.value || t.type}'`, "expected-value");
        const placeholder: Literal = {
            kind: "Literal",
            range: { start: t.start, end: t.end },
            raw: "",
            literalKind: "string",
        };
        this.advance();
        return placeholder;
    }

    private parseLiteral(): Literal {
        const t = this.advance();
        const lit: Literal = { kind: "Literal", range: { start: t.start, end: t.end }, raw: t.value, literalKind: "string" };
        switch (t.type) {
            case "String":   lit.literalKind = "string"; lit.stringValue = decodeString(t.value.slice(1, -1)); break;
            case "Char":     lit.literalKind = "char"; lit.stringValue = t.value.slice(1, -1); break;
            case "Number":   lit.literalKind = "number"; lit.numericValue = parseNumeric(t.value); break;
            case "Quantity": {
                lit.literalKind = "quantity";
                const m = /^([-+]?(?:0x[\da-fA-F_]+|0b[01_]+|0o[0-7_]+|\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][-+]?\d+)?))(.+)$/.exec(t.value);
                if (m) { lit.numericValue = parseNumeric(m[1]); lit.unit = m[2]; }
                break;
            }
            case "Identifier":
                if (t.value === "true" || t.value === "false") {
                    lit.literalKind = "boolean";
                    lit.boolValue = t.value === "true";
                }
                break;
            default: break;
        }
        return lit;
    }

    private parseMultiple(): MultipleExpr {
        const maxLit = this.parseLiteral();
        if (this.match("Range")) {
            this.advance();
            const highTok = this.peek();
            if (highTok.type === "Number") {
                const high = this.parseLiteral();
                return {
                    kind: "MultipleExpr",
                    range: { start: maxLit.range.start, end: high.range.end },
                    min: maxLit,
                    max: high,
                };
            }
            this.error(highTok.start, highTok.end, "Expected upper bound after '..'", "expected-bound");
        }
        return {
            kind: "MultipleExpr",
            range: maxLit.range,
            max: maxLit,
        };
    }

    private parseIdentifierList(): IdentifierList {
        const open = this.advance();              // '['
        const items: Identifier[] = [];
        while (!this.match("RBracket") && this.peek().type !== "EOF") {
            if (this.match("Comma")) { this.advance(); continue; }
            const t = this.peek();
            if (t.type === "Identifier") {
                this.advance();
                items.push({ kind: "Identifier", range: { start: t.start, end: t.end }, name: t.value });
            } else {
                this.error(t.start, t.end,
                    `Expected identifier in list, got '${t.value || t.type}'`, "expected-ident-in-list");
                this.advance();
            }
        }
        const close = this.consume("RBracket", "Expected ']' to close identifier list");
        return { kind: "IdentifierList", range: { start: open.start, end: close?.end ?? open.end }, items };
    }

    private parseVariantDefaultBlock(): VariantDefaultBlock {
        const open = this.advance();              // '{'
        const arms: VariantArm[] = [];
        while (!this.match("RBrace") && this.peek().type !== "EOF") {
            const condTok = this.peek();
            if (condTok.type !== "Identifier") {
                this.error(condTok.start, condTok.end, "Expected condition keyword or 'default'");
                this.advance();
                continue;
            }
            this.advance();
            let condName: string | undefined;
            let op: "==" | "!=" | undefined;
            let condValue: string | undefined;
            if (condTok.value !== "default") {
                const nameTok = this.consume("Identifier", `Expected name after '${condTok.value}'`);
                if (nameTok) condName = nameTok.value;
                if (this.match("Eq") || this.match("NotEq")) {
                    const opTok = this.advance();
                    const valTok = this.consume("Identifier", "Expected value after operator");
                    if (valTok) { op = opTok.value as "==" | "!="; condValue = valTok.value; }
                }
            }
            this.consume("Colon", `Expected ':' after condition '${condTok.value}'`);
            const value = this.parseValueExpr("default-arm");
            if (this.match("Comma")) this.advance();
            arms.push({
                kind: "VariantArm",
                range: { start: condTok.start, end: value.range.end },
                condKind: condTok.value,
                condName, op, condValue,
                value,
            });
        }
        const close = this.consume("RBrace", "Expected '}' to close variant-default block");
        return { kind: "VariantDefaultBlock", range: { start: open.start, end: close?.end ?? open.end }, arms };
    }

    private parseTypeExpr(): TypeExpr | CompoundTypeExpr | ChoiceTypeExpr {
        const t = this.peek();
        if (t.type === "LBrace") return this.parseCompoundType();
        if (t.type === "LParen") return this.parseChoiceType();

        const base = this.advance();
        const node: TypeExpr = {
            kind: "TypeExpr",
            range: { start: base.start, end: base.end },
            baseName: base.value,
            baseRange: { start: base.start, end: base.end },
            args: [],
        };
        if (this.match("LParen")) {
            this.advance();
            while (!this.match("RParen") && this.peek().type !== "EOF") {
                const argTok = this.peek();
                if (argTok.type !== "Identifier" && argTok.type !== "Number" &&
                    argTok.type !== "Quantity") {
                    this.error(argTok.start, argTok.end, `Unexpected token in type arguments`, "bad-type-arg");
                    this.advance();
                    continue;
                }
                this.advance();
                const arg: TypeArg = { kind: "TypeArg", range: { start: argTok.start, end: argTok.end }, name: argTok.value };
                if (this.match("Assign")) {
                    this.advance();
                    const valTok = this.peek();
                    if (valTok.type === "Number" || valTok.type === "Identifier") {
                        this.advance();
                        arg.value = valTok.type === "Number"
                            ? { kind: "Literal", range: { start: valTok.start, end: valTok.end }, raw: valTok.value, literalKind: "number", numericValue: parseNumeric(valTok.value) }
                            : { kind: "Identifier", range: { start: valTok.start, end: valTok.end }, name: valTok.value };
                        arg.range.end = valTok.end;
                    }
                }
                node.args.push(arg);
                if (this.match("Comma")) this.advance();
            }
            const close = this.consume("RParen", "Expected ')' to close type arguments");
            if (close) node.range.end = close.end;
        }
        // optional `in lo..hi`
        const nextAfterIn = this.peek(1);
        if (this.match("Identifier", "in") &&
            (nextAfterIn.type === "Number" || nextAfterIn.type === "Quantity")) {
            const inTok = this.advance();
            let low: Literal | undefined;
            let high: Literal | undefined;
            const lowTok = this.peek();
            if (lowTok.type === "Number" || lowTok.type === "Quantity") {
                low = this.parseLiteral();
            } else {
                this.error(lowTok.start, lowTok.end, "Expected lower bound after 'in'", "expected-bound");
            }
            this.consume("Range", "Expected '..' between range bounds");
            const highTok = this.peek();
            if (highTok.type === "Number" || highTok.type === "Quantity") {
                high = this.parseLiteral();
            } else {
                this.error(highTok.start, highTok.end, "Expected upper bound after '..'", "expected-bound");
            }
            if (low && high) {
                node.rangeConstraint = { kind: "RangeExpr", range: { start: inTok.start, end: high.range.end }, low, high };
                node.range.end = high.range.end;
            }
        }
        // optional `units [ a, b, c ]` (v2) — restrict accepted unit suffixes
        if (this.match("Identifier", "units") && this.peek(1).type === "LBracket") {
            this.advance(); // units
            this.advance(); // [
            node.units = [];
            while (!this.match("RBracket") && this.peek().type !== "EOF") {
                const ut = this.peek();
                if (ut.type === "Identifier") node.units.push(ut.value);
                this.advance();
            }
            const close = this.consume("RBracket", "Expected ']' to close units list");
            if (close) node.range.end = close.end;
        }
        return node;
    }

    /* --------- resync ---------- */

    private resyncToTopLevel(): void {
        while (this.peek().type !== "EOF") {
            const t = this.peek();
            if (t.type === "Identifier" &&
                (BLOCK_KEYWORDS.has(t.value) || CONDITIONAL_KEYWORDS.has(t.value))) return;
            if (t.type === "Directive") return;
            this.advance();
        }
    }
}

/* ============================================================================
 *  helpers
 * ========================================================================== */

function isTypeStart(name: string): boolean {
    return PRIMITIVE_TYPES.has(name) || QUANTITY_TYPES.has(name);
}

function parseNumeric(raw: string): number {
    const cleaned = raw.replace(/_/g, "");
    if (/^[-+]?0x/i.test(cleaned)) return parseInt(cleaned, 16);
    if (/^[-+]?0b/i.test(cleaned)) return parseInt(cleaned.replace(/^([-+]?)0b/i, "$1"), 2);
    if (/^[-+]?0o/i.test(cleaned)) return parseInt(cleaned.replace(/^([-+]?)0o/i, "$1"), 8);
    return Number(cleaned);
}

function decodeString(raw: string): string {
    return raw.replace(/\\(.)/g, (_, c) => {
        switch (c) {
            case "n": return "\n"; case "t": return "\t"; case "r": return "\r";
            case "\\": return "\\"; case "\"": return "\""; case "'": return "'";
            case "0": return "\0"; default: return c;
        }
    });
}
