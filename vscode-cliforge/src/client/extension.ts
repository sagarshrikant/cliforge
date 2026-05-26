/* ============================================================================
 *  extension.ts
 *  ----------------------------------------------------------------------------
 *  VS Code extension entry point. Activated on the `cliforge` language id;
 *  spawns the LSP server (out/server/server.js) and wires it up.
 * ========================================================================== */

import * as path from "path";
import { ExtensionContext, workspace } from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

export function activate(context: ExtensionContext): void {
    const serverModule = context.asAbsolutePath(
        path.join("out", "server", "server.js"),
    );

    const debugOptions = { execArgv: ["--nolazy", "--inspect=6009"] };

    const serverOptions: ServerOptions = {
        run:   { module: serverModule, transport: TransportKind.ipc },
        debug: { module: serverModule, transport: TransportKind.ipc, options: debugOptions },
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: "file", language: "cliforge" }],
        synchronize: {
            configurationSection: "cliforge",
            fileEvents: workspace.createFileSystemWatcher("**/*.cf"),
        },
    };

    client = new LanguageClient(
        "cliforge",
        "cliforge Language Server",
        serverOptions,
        clientOptions,
    );

    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
