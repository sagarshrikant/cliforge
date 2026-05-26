/**
 *  Type shim for vscode-languageclient/node.
 *
 *  vscode-languageclient@9 ships no .d.ts for its /node entry point.
 *  This declaration re-exports everything from the common package root
 *  and adds the node-only symbols (LanguageClient, ServerOptions,
 *  TransportKind) that extension.ts needs.
 */
declare module "vscode-languageclient/node" {
    // Re-export everything from the common package (LanguageClientOptions, etc.)
    export * from "vscode-languageclient";

    // Node-specific transport kinds
    export enum TransportKind {
        stdio = 0,
        ipc   = 1,
        pipe  = 2,
        socket = 3,
    }

    // Node-specific server startup options
    export interface NodeModule {
        module: string;
        transport?: TransportKind;
        args?: string[];
        options?: { execArgv?: string[]; cwd?: string; env?: NodeJS.ProcessEnv };
    }

    export type ServerOptions =
        | NodeModule
        | { run: NodeModule; debug: NodeModule };

    // Node-specific LanguageClient (extends BaseLanguageClient)
    export class LanguageClient {
        constructor(
            id: string,
            name: string,
            serverOptions: ServerOptions,
            clientOptions: import("vscode-languageclient").LanguageClientOptions,
        );
        start(): Promise<void>;
        stop(): Promise<void>;
    }

    export class SettingMonitor {
        constructor(client: LanguageClient, setting: string);
        start(): import("vscode").Disposable;
    }
}
