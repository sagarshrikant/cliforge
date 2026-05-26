# UTest Agent VSCode Extension

Adds a proper VSCode extension on top of the Python agent — status bar, commands, webview report panel.

## What You Get

```
┌─────────────────────────────────────────────────────────────┐
│  Status bar (bottom):  ⊙ UTest: L92% F87% B81%             │
│                         └─ live from lcov .info file        │
│                                                             │
│  Right-click a .c file:                                     │
│    → UTest: Analyze Current File                            │
│    → UTest: Full Coverage Scan                              │
│                                                             │
│  Command palette (Ctrl+Shift+P):                            │
│    → All agent modes listed                                 │
│                                                             │
│  Keyboard shortcuts:                                        │
│    Ctrl+Shift+U       → Analyze changed files               │
│    Ctrl+Shift+Alt+U   → Analyze current file                │
│    Ctrl+Shift+Alt+P   → Pre-PR gate check                   │
│                                                             │
│  Report panel: AI output opens beside the editor,           │
│  formatted with VSCode's color theme                        │
└─────────────────────────────────────────────────────────────┘
```

## Install (two steps)

### Step 1 — Package the extension
```bash
# Install the VSCode extension packaging tool (one-time)
npm install -g @vscode/vsce

# Package from the extension directory
cd tools/vscode-utest-extension
npm init -y          # creates a minimal package-lock.json
vsce package         # creates utest-agent-1.0.0.vsix
```

### Step 2 — Install into VSCode
```bash
code --install-extension utest-agent-1.0.0.vsix
```
Or: VSCode → Extensions sidebar → `...` menu → "Install from VSIX"

**For teammates:** commit the `.vsix` file, they just run step 2.

## Install companion extension (recommended)

**Coverage Gutters** shows red/green lines directly in the editor from your lcov data.  
Install from the VSCode marketplace: `ryanluker.vscode-coverage-gutters`

Then copy the settings from `recommended-settings.json` into your `.vscode/settings.json`.

After a build, it looks like this in the editor:
```c
      int app_init(void) {       ← green gutter: covered
  ●   {
  ●       config = load();       ← red gutter: NEVER executed
  ●       if (!config) return -1;
          ...
```

## Settings

All configurable via `File → Preferences → Settings → search "UTest"`:

| Setting | Default | Description |
|---------|---------|-------------|
| `utestAgent.llmBackend` | `claude` | `claude` or `ollama` |
| `utestAgent.ollamaModel` | `qwen2.5-coder:7b` | Which Ollama model |
| `utestAgent.buildDir` | `build` | CMake build directory |
| `utestAgent.coverageDir` | `coverage_report` | lcov output directory |
| `utestAgent.pythonPath` | `python3` | Python executable |
| `utestAgent.autoRefreshStatusBar` | `true` | Auto-refresh % when .info changes |

## Switching between Claude and Ollama

Change in settings, or just pick the right command:
- "UTest: Analyze Changed Files" → uses setting
- "UTest: Analyze Changed Files (Ollama)" → forces Ollama regardless of setting
- "UTest: Full Pipeline (Ollama local)" → full pipeline, local LLM

## Development (modifying the extension)

No build step needed — it's plain JavaScript.

To test locally without packaging:
1. Open `tools/vscode-utest-extension/` as a workspace in VSCode
2. Press `F5` → opens a new "Extension Development Host" window
3. The extension is live in that window

To reload after edits:
- `Ctrl+Shift+P` → "Developer: Reload Window"
