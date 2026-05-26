/* ============================================================================
 *  server.ts
 *  ----------------------------------------------------------------------------
 *  Language Server entry point. Wires the LSP protocol to our parser /
 *  validator / providers.
 *
 *  The server speaks LSP over Node IPC; the client (extension.ts) spawns this
 *  module as a child process. We use the standard request-driven model with
 *  incremental document sync.
 * ========================================================================== */

import {
    createConnection,
    ProposedFeatures,
    TextDocuments,
    TextDocumentSyncKind,
    InitializeResult,
    Diagnostic,
    DiagnosticSeverity,
    DocumentSymbolParams,
    CompletionParams,
    HoverParams,
    DefinitionParams,
    CompletionItem,
    Hover,
    Location,
    DocumentSymbol,
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";

import { parse, ParseDiagnostic } from "./parser";
import { validate } from "./validator";
import { buildCompletions } from "./completion";
import { buildHover } from "./hover";
import { buildSymbols } from "./symbols";
import { buildDefinition } from "./definitions";
import { SchemaFile } from "./ast";

/* ---------------------------------------------------------------------------- */

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

interface AnalysisCacheEntry {
    version: number;
    file: SchemaFile;
    diagnostics: ParseDiagnostic[];
}
const cache = new Map<string, AnalysisCacheEntry>();

let validationEnabled = true;

connection.onInitialize((): InitializeResult => {
    return {
        capabilities: {
            textDocumentSync: TextDocumentSyncKind.Incremental,
            completionProvider: {
                triggerCharacters: ["@", " ", "=", "(", ".", "-"],
                resolveProvider: false,
            },
            hoverProvider: true,
            documentSymbolProvider: true,
            definitionProvider: true,
        },
    };
});

connection.onInitialized(() => {
    connection.console.info("cliforge LSP initialized.");
});

/* ---- configuration -------------------------------------------------------- */

connection.onDidChangeConfiguration(change => {
    const cfg = (change?.settings as { cliforge?: { validation?: { enable?: boolean } } } | undefined)?.cliforge;
    validationEnabled = cfg?.validation?.enable !== false;
    for (const doc of documents.all()) analyze(doc);
});

/* ---- document lifecycle --------------------------------------------------- */

documents.onDidOpen(e => analyze(e.document));
documents.onDidChangeContent(e => analyze(e.document));
documents.onDidClose(e => {
    cache.delete(e.document.uri);
    connection.sendDiagnostics({ uri: e.document.uri, diagnostics: [] });
});

function analyze(doc: TextDocument): void {
    const text = doc.getText();
    const { file, diagnostics: parseDiags } = parse(text);
    const semDiags = validationEnabled ? validate(file) : [];
    const all = [...parseDiags, ...semDiags];

    cache.set(doc.uri, { version: doc.version, file, diagnostics: all });

    connection.sendDiagnostics({
        uri: doc.uri,
        diagnostics: all.map(d => toLspDiagnostic(doc, d)),
    });
}

function toLspDiagnostic(doc: TextDocument, d: ParseDiagnostic): Diagnostic {
    return {
        severity: d.severity === "error" ? DiagnosticSeverity.Error : DiagnosticSeverity.Warning,
        message: d.message,
        range: {
            start: doc.positionAt(d.range.start),
            end:   doc.positionAt(d.range.end),
        },
        source: "cliforge",
        code: d.code,
    };
}

/* ---- providers ------------------------------------------------------------ */

connection.onCompletion((params: CompletionParams): CompletionItem[] => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return [];
    const entry = cache.get(params.textDocument.uri);
    return buildCompletions(params.textDocument, doc, params.position, entry?.file ?? null);
});

connection.onHover((params: HoverParams): Hover | null => {
    const doc = documents.get(params.textDocument.uri);
    if (!doc) return null;
    return buildHover(doc, params.position);
});

connection.onDocumentSymbol((params: DocumentSymbolParams): DocumentSymbol[] => {
    const doc = documents.get(params.textDocument.uri);
    const entry = cache.get(params.textDocument.uri);
    if (!doc || !entry) return [];
    return buildSymbols(doc, entry.file);
});

connection.onDefinition((params: DefinitionParams): Location | Location[] | null => {
    const doc = documents.get(params.textDocument.uri);
    const entry = cache.get(params.textDocument.uri);
    if (!doc || !entry) return null;
    return buildDefinition(doc, params.position, entry.file);
});

/* ---------------------------------------------------------------------------- */

documents.listen(connection);
connection.listen();
