// extension.js — UTest Agent VSCode Extension
// Plain JavaScript (no TypeScript compile step needed).
//
// Provides:
//   • Status bar item showing live coverage % (reads coverage_filtered.info)
//   • Command palette entries for all agent modes
//   • Right-click menu on .c files
//   • Keyboard shortcuts
//   • Webview panel showing the AI report with syntax highlighting
//   • File watcher: auto-refreshes coverage % when .info file changes

"use strict";

const vscode  = require("vscode");
const cp      = require("child_process");
const path    = require("path");
const fs      = require("fs");

// ── Module-level state ────────────────────────────────────────────
let statusBarItem;          // coverage % in bottom bar
let outputChannel;          // dedicated output panel
let reportPanel;            // webview panel for AI report
let coverageWatcher;        // fs.FSWatcher on coverage_filtered.info
let lastReport = "";        // cache last AI report text


// ══════════════════════════════════════════════════════════════════
// Activation — called once when the extension loads
// ══════════════════════════════════════════════════════════════════

function activate(context) {
    outputChannel = vscode.window.createOutputChannel("UTest Agent");

    // ── Status bar item ─────────────────────────────────────────
    statusBarItem = vscode.window.createStatusBarItem(
        vscode.StatusBarAlignment.Left, 100
    );
    statusBarItem.command  = "utest.showReport";
    statusBarItem.tooltip  = "UTest: click to show last AI report";
    statusBarItem.text     = "$(beaker) UTest";
    statusBarItem.show();
    context.subscriptions.push(statusBarItem);

    // Initial coverage read
    refreshCoverageStatus();

    // ── Watch coverage file for changes ─────────────────────────
    const cfg = getConfig();
    const infoPath = path.join(workspaceRoot(), cfg.coverageDir,
                               "coverage_filtered.info");
    if (cfg.autoRefreshStatusBar) {
        _watchCoverageFile(infoPath, context);
    }

    // ── Register all commands ────────────────────────────────────
    const cmds = [
        ["utest.analyzeChanges",       () => runAgent([])],
        ["utest.analyzeChangesOllama", () => runAgent([], { llm: "ollama" })],
        ["utest.fullPipeline",         () => runAgent(["--with-build"])],
        ["utest.fullPipelineOllama",   () => runAgent(["--with-build"], { llm: "ollama" })],
        ["utest.reCoverageOnly",       () => runAgent(["--coverage-only"])],
        ["utest.prePrCheck",           () => runAgent(["--prepr", "--output", "utest_prepr_report.md"])],
        ["utest.analyzeCurrentFile",   () => runAgent(["--file", currentFilePath()])],
        ["utest.scanCurrentFile",      () => runAgent(["--scan", currentFilePath()])],
        ["utest.openHtmlReport",       openHtmlReport],
        ["utest.showReport",           showReportPanel],
        ["utest.listModels",           () => runAgent(["--list-models"])],
    ];

    cmds.forEach(([id, fn]) => {
        context.subscriptions.push(vscode.commands.registerCommand(id, fn));
    });

    outputChannel.appendLine("UTest Agent extension activated.");
    outputChannel.appendLine(`Workspace: ${workspaceRoot()}`);
}

function deactivate() {
    if (coverageWatcher) coverageWatcher.close();
}


// ══════════════════════════════════════════════════════════════════
// Core: run the Python agent as a child process
// ══════════════════════════════════════════════════════════════════

/**
 * Run agent.py with the given extra args.
 * Streams output to the Output Channel AND captures it for the report panel.
 *
 * @param {string[]} extraArgs   - e.g. ["--with-build"] or ["--file", "src/app.c"]
 * @param {object}   overrides   - { llm: "ollama" } to override settings
 */
async function runAgent(extraArgs = [], overrides = {}) {
    const cfg   = getConfig();
    const root  = workspaceRoot();
    if (!root) {
        vscode.window.showErrorMessage("UTest Agent: No workspace folder open.");
        return;
    }

    const llm   = overrides.llm || cfg.llmBackend;
    const model = overrides.model || cfg.ollamaModel;
    const agent = path.join(root, cfg.agentPath);

    if (!fs.existsSync(agent)) {
        vscode.window.showErrorMessage(
            `agent.py not found at: ${agent}\nCheck utestAgent.agentPath in settings.`
        );
        return;
    }

    // Build full command
    const args = [
        agent,
        "--llm", llm,
        "--build-dir", cfg.buildDir,
        "--coverage-dir", cfg.coverageDir,
        ...extraArgs,
    ];
    if (llm === "ollama") {
        args.push("--model", model, "--ollama-url", cfg.ollamaUrl);
    }

    const label = _labelFromArgs(extraArgs, llm);
    statusBarItem.text    = `$(sync~spin) UTest: running…`;
    statusBarItem.tooltip = label;

    outputChannel.show(true);   // focus=true
    outputChannel.appendLine("");
    outputChannel.appendLine(`${"─".repeat(60)}`);
    outputChannel.appendLine(`▶  ${label}`);
    outputChannel.appendLine(`   ${cfg.pythonPath} ${args.join(" ")}`);
    outputChannel.appendLine(`${"─".repeat(60)}`);

    lastReport = "";
    const collected = [];

    return new Promise((resolve) => {
        const proc = cp.spawn(cfg.pythonPath, args, {
            cwd: root,
            env: { ...process.env },    // inherit ANTHROPIC_API_KEY etc.
            shell: false,
        });

        proc.stdout.on("data", (chunk) => {
            const text = chunk.toString();
            outputChannel.append(text);
            collected.push(text);
        });

        proc.stderr.on("data", (chunk) => {
            outputChannel.append(chunk.toString());
        });

        proc.on("close", (code) => {
            lastReport = collected.join("");
            const ok   = code === 0;
            const icon = ok ? "$(check)" : "$(error)";

            outputChannel.appendLine(`\n${"─".repeat(60)}`);
            outputChannel.appendLine(`${icon}  Exit code: ${code}`);
            outputChannel.appendLine(`${"─".repeat(60)}`);

            // Update status bar
            refreshCoverageStatus();

            // Offer to show report panel if there's substantial output
            if (lastReport.length > 200) {
                _offerReportPanel(label, ok);
            }

            if (!ok && code !== null) {
                vscode.window.showWarningMessage(
                    `UTest Agent: ${label} exited with code ${code}. See Output panel.`,
                    "Show Output"
                ).then(sel => { if (sel) outputChannel.show(); });
            }

            resolve(code);
        });

        proc.on("error", (err) => {
            outputChannel.appendLine(`❌ Failed to start: ${err.message}`);
            vscode.window.showErrorMessage(
                `Cannot start ${cfg.pythonPath}. Is Python installed?\n${err.message}`
            );
            statusBarItem.text = "$(error) UTest";
            resolve(-1);
        });
    });
}


// ══════════════════════════════════════════════════════════════════
// Status bar: parse coverage % from lcov .info file
// ══════════════════════════════════════════════════════════════════

function refreshCoverageStatus() {
    const cfg  = getConfig();
    const root = workspaceRoot();
    if (!root) return;

    // Try filtered first, then raw
    const candidates = [
        path.join(root, cfg.coverageDir, "coverage_filtered.info"),
        path.join(root, cfg.coverageDir, "coverage.info"),
    ];

    for (const infoPath of candidates) {
        if (!fs.existsSync(infoPath)) continue;
        const result = _parseLcovSummary(infoPath);
        if (result) {
            const { lines, funcs, branches } = result;
            const icon  = funcs >= 100 ? "$(check)" : "$(beaker)";
            const color = funcs >= 100
                ? new vscode.ThemeColor("statusBarItem.prominentBackground")
                : undefined;

            statusBarItem.text            = `${icon} UTest: L${lines.toFixed(0)}% F${funcs.toFixed(0)}% B${branches.toFixed(0)}%`;
            statusBarItem.tooltip         = `Lines: ${lines.toFixed(1)}%  Functions: ${funcs.toFixed(1)}%  Branches: ${branches.toFixed(1)}%\nClick to open last AI report`;
            statusBarItem.backgroundColor = color;
            return;
        }
    }

    // No coverage data yet
    statusBarItem.text    = "$(beaker) UTest: no data";
    statusBarItem.tooltip = "Run 'UTest: Full Pipeline' to generate coverage data";
}

/**
 * Parse an lcov .info file and return {lines, funcs, branches} percentages.
 * Reads the file directly — no subprocess needed.
 */
function _parseLcovSummary(infoPath) {
    try {
        const content = fs.readFileSync(infoPath, "utf8");
        let lh = 0, lf = 0, fnh = 0, fnf = 0, brh = 0, brf = 0;

        for (const line of content.split("\n")) {
            const kv = line.trim().split(":");
            if (kv.length < 2) continue;
            const [key, val] = [kv[0], parseInt(kv[1])];
            if (key === "LH")  lh  += val;
            if (key === "LF")  lf  += val;
            if (key === "FNH") fnh += val;
            if (key === "FNF") fnf += val;
            if (key === "BRH") brh += val;
            if (key === "BRF") brf += val;
        }

        if (lf === 0) return null;
        return {
            lines:    lf  ? (lh  / lf  * 100) : 100,
            funcs:    fnf ? (fnh / fnf * 100) : 100,
            branches: brf ? (brh / brf * 100) : 100,
        };
    } catch {
        return null;
    }
}

function _watchCoverageFile(infoPath, context) {
    // Watch the directory — the file may not exist yet
    const dir = path.dirname(infoPath);
    if (!fs.existsSync(dir)) return;

    try {
        coverageWatcher = fs.watch(dir, (event, filename) => {
            if (filename && filename.includes("coverage")) {
                refreshCoverageStatus();
            }
        });
        context.subscriptions.push({ dispose: () => coverageWatcher.close() });
    } catch {
        // Directory doesn't exist yet — watcher will be set up when data arrives
    }
}


// ══════════════════════════════════════════════════════════════════
// Webview report panel — shows the last AI report as formatted HTML
// ══════════════════════════════════════════════════════════════════

function showReportPanel() {
    if (!lastReport && !_loadCachedReport()) {
        vscode.window.showInformationMessage(
            "No UTest report yet. Run an analysis first.",
            "Run Analysis"
        ).then(sel => { if (sel) runAgent([]); });
        return;
    }

    const column = vscode.ViewColumn.Beside;  // open beside the editor

    if (reportPanel) {
        reportPanel.reveal(column);
    } else {
        reportPanel = vscode.window.createWebviewPanel(
            "utestReport",
            "UTest Agent Report",
            column,
            { enableScripts: true, retainContextWhenHidden: true }
        );
        reportPanel.onDidDispose(() => { reportPanel = undefined; });
    }

    reportPanel.webview.html = _buildReportHtml(lastReport || _loadCachedReport());
}

function _offerReportPanel(label, success) {
    const msg   = success ? `UTest: ${label} complete.` : `UTest: ${label} finished.`;
    const btn   = "Show Report";
    vscode.window.showInformationMessage(msg, btn, "Dismiss")
        .then(sel => { if (sel === btn) showReportPanel(); });
}

function _loadCachedReport() {
    const root = workspaceRoot();
    if (!root) return "";
    const reportFile = path.join(root, "utest_prepr_report.md");
    try { return fs.existsSync(reportFile) ? fs.readFileSync(reportFile, "utf8") : ""; }
    catch { return ""; }
}

/**
 * Convert the agent's markdown output to HTML for the webview.
 * Uses a minimal built-in renderer — no external dependencies.
 */
function _buildReportHtml(markdownText) {
    // Simple markdown → HTML conversion for the report's patterns
    const escaped = markdownText
        .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

    const html = escaped
        // Code blocks
        .replace(/```(\w*)\n([\s\S]*?)```/g, '<pre><code class="lang-$1">$2</code></pre>')
        // Inline code
        .replace(/`([^`]+)`/g, "<code>$1</code>")
        // Headers
        .replace(/^### (.+)$/gm, "<h3>$1</h3>")
        .replace(/^## (.+)$/gm,  "<h2>$1</h2>")
        .replace(/^# (.+)$/gm,   "<h1>$1</h1>")
        // Bold
        .replace(/\*\*(.+?)\*\*/g, "<strong>$1</strong>")
        // Checkmarks / icons
        .replace(/✅/g, '<span class="ok">✅</span>')
        .replace(/❌/g, '<span class="fail">❌</span>')
        .replace(/⚠️/g,  '<span class="warn">⚠️</span>')
        // Bullet lists
        .replace(/^- (.+)$/gm,  "<li>$1</li>")
        .replace(/(<li>.*<\/li>\n?)+/g, "<ul>$&</ul>")
        // Horizontal rules
        .replace(/^─+$/gm, "<hr>")
        // Paragraphs (double newlines)
        .replace(/\n\n/g, "</p><p>")
        // Preserve single newlines inside paragraphs
        .replace(/\n/g, "<br>");

    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>UTest Agent Report</title>
<style>
  body {
    font-family: var(--vscode-editor-font-family, monospace);
    font-size: var(--vscode-editor-font-size, 13px);
    color: var(--vscode-editor-foreground);
    background: var(--vscode-editor-background);
    padding: 16px 24px;
    max-width: 900px;
    line-height: 1.6;
  }
  h1 { color: var(--vscode-textLink-foreground); border-bottom: 1px solid #555; padding-bottom: 6px; }
  h2 { color: var(--vscode-textLink-foreground); margin-top: 24px; }
  h3 { color: var(--vscode-descriptionForeground); margin-top: 16px; }
  code {
    font-family: var(--vscode-editor-font-family, monospace);
    background: var(--vscode-textCodeBlock-background, #1e1e1e);
    color: var(--vscode-textPreformat-foreground, #ce9178);
    padding: 1px 5px;
    border-radius: 3px;
    font-size: 0.92em;
  }
  pre {
    background: var(--vscode-textCodeBlock-background, #1e1e1e);
    border: 1px solid var(--vscode-panel-border, #444);
    border-radius: 6px;
    padding: 12px 16px;
    overflow-x: auto;
  }
  pre code { background: none; padding: 0; }
  ul { padding-left: 20px; }
  li { margin: 4px 0; }
  hr { border: none; border-top: 1px solid var(--vscode-panel-border, #444); margin: 20px 0; }
  strong { color: var(--vscode-textLink-foreground); }
  .ok   { color: #4ec9b0; }
  .fail { color: #f48771; }
  .warn { color: #cca700; }
  .timestamp {
    font-size: 0.8em;
    color: var(--vscode-descriptionForeground);
    margin-bottom: 16px;
  }
  /* Syntax: diff-like colors for code blocks */
  .lang-diff .added   { color: #6a9955; }
  .lang-diff .removed { color: #f48771; }
</style>
</head>
<body>
<div class="timestamp">Generated: ${new Date().toLocaleString()}</div>
<p>${html}</p>
</body>
</html>`;
}


// ══════════════════════════════════════════════════════════════════
// Open lcov HTML report in browser
// ══════════════════════════════════════════════════════════════════

function openHtmlReport() {
    const cfg      = getConfig();
    const root     = workspaceRoot();
    const htmlPath = path.join(root, cfg.coverageDir, "html", "index.html");

    if (!fs.existsSync(htmlPath)) {
        vscode.window.showWarningMessage(
            "HTML coverage report not found. Run 'UTest: Full Pipeline' first.",
            "Run Pipeline"
        ).then(sel => { if (sel) runAgent(["--with-build"]); });
        return;
    }

    const uri = vscode.Uri.file(htmlPath);
    vscode.env.openExternal(uri);
}


// ══════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════

function getConfig() {
    const c = vscode.workspace.getConfiguration("utestAgent");
    return {
        llmBackend:           c.get("llmBackend",           "claude"),
        ollamaModel:          c.get("ollamaModel",          "qwen2.5-coder:7b"),
        ollamaUrl:            c.get("ollamaUrl",            "http://localhost:11434"),
        agentPath:            c.get("agentPath",            "tools/utest_agent/agent.py"),
        buildDir:             c.get("buildDir",             "build"),
        coverageDir:          c.get("coverageDir",          "coverage_report"),
        pythonPath:           c.get("pythonPath",           "python3"),
        autoRefreshStatusBar: c.get("autoRefreshStatusBar", true),
    };
}

function workspaceRoot() {
    return vscode.workspace.workspaceFolders?.[0]?.uri?.fsPath ?? "";
}

function currentFilePath() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showErrorMessage("UTest Agent: No file is open.");
        return "";
    }
    const root = workspaceRoot();
    return path.relative(root, editor.document.uri.fsPath);
}

function _labelFromArgs(extraArgs, llm) {
    const backend = llm === "ollama" ? " [Ollama]" : " [Claude]";
    if (extraArgs.includes("--with-build"))    return `Full Pipeline${backend}`;
    if (extraArgs.includes("--prepr"))         return `Pre-PR Gate${backend}`;
    if (extraArgs.includes("--coverage-only")) return `Coverage Re-analysis${backend}`;
    if (extraArgs.includes("--file"))          return `File Analysis${backend}`;
    if (extraArgs.includes("--scan"))          return `Coverage Scan${backend}`;
    if (extraArgs.includes("--list-models"))   return "List Ollama Models";
    return `Changed Files Analysis${backend}`;
}

module.exports = { activate, deactivate };
