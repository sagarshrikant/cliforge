# UTest Agent

AI-powered unit test advisor for cliforge. Analyses `git diff` output
and lcov coverage data then suggests GTest additions, removals, and
updates — using either the Claude API (cloud) or a local Ollama model.

## Setup

### Linux / WSL (Ubuntu 22.04+)

```bash
# Build tools and coverage support
sudo apt install -y cmake ninja-build gcc g++ lcov python3 python3-pip

# zstd is required by Ollama on Ubuntu — install it before setup_ollama.sh
sudo apt install -y zstd

# Python dependencies
# Ubuntu 23.04+ protects system Python — use --break-system-packages
# (safe on a dev machine / WSL where you own the environment)
pip3 install --break-system-packages -r tools/utest_agent/requirements.txt
```

If you prefer an isolated Python environment instead:

```bash
python3 -m venv .venv          # may need: sudo apt install python3-venv
source .venv/bin/activate
pip install -r tools/utest_agent/requirements.txt
# Re-run 'source .venv/bin/activate' at the start of each new terminal session
```

### macOS

```bash
brew install cmake ninja lcov python3
pip3 install -r tools/utest_agent/requirements.txt
```

### Claude API key

```bash
export ANTHROPIC_API_KEY=sk-ant-...
# Make it permanent:
echo 'export ANTHROPIC_API_KEY=sk-ant-...' >> ~/.bashrc
```

### Ollama (local, offline mode)

```bash
# One-time install — detects OS, pulls qwen2.5-coder:7b (~5 GB)
bash tools/utest_agent/setup_ollama.sh
```

Ollama runs as a background service. On WSL it does not auto-start, so
run this once per terminal session before using `--llm ollama`:

```bash
ollama serve > /tmp/ollama.log 2>&1 &
```

If you see `bind: address already in use`, Ollama is already running — no
action needed. To verify:

```bash
curl -s http://localhost:11434/api/tags   # should return JSON with model list
```

## Quick start

```bash
# Fast path: analyse only what changed since HEAD (Claude)
python3 tools/utest_agent/agent.py

# Same, but with local Ollama (no API key needed)
python3 tools/utest_agent/agent.py --llm ollama

# Full pipeline: build → test → coverage → AI suggestions
python3 tools/utest_agent/agent.py --with-build

# Save report to file
python3 tools/utest_agent/agent.py --output utest_report.md
```

> **WSL tip:** clone the repo into WSL's native filesystem (`~/dev/cliforge`)
> rather than working from `/mnt/c/...`. The Windows↔Linux filesystem boundary
> makes builds 5–10× slower and can break lcov coverage capture.

## Modes

| Command | What it does |
|---------|-------------|
| `agent.py` | Diff-only (fast, no build) |
| `agent.py --with-build` | Build → CTest → lcov → AI |
| `agent.py --coverage-only` | Re-parse last coverage.info, no rebuild |
| `agent.py --prepr` | Pre-PR gate — exits 1 if not ready |
| `agent.py --file src/cf_lex.c` | Analyse one file |
| `agent.py --scan src/cf_lex.c` | Full coverage scan of one file |
| `agent.py --llm ollama` | Use local Ollama instead of Claude |
| `agent.py --list-models` | Show recommended Ollama models |

## LLM backends

**Claude (default)** — best quality, needs `ANTHROPIC_API_KEY`

**Ollama (local)** — offline, free, slower; needs Ollama installed:
```bash
# One-time setup (Linux/macOS)
bash tools/utest_agent/setup_ollama.sh

# Then use
python tools/utest_agent/agent.py --llm ollama
```

## VSCode integration

Two ways to run from VSCode:

1. **Tasks** — open `.vscode/tasks.json` tasks via `Ctrl+Shift+P → Tasks: Run Task`
2. **Extension** — install `tools/vscode-utest-extension/` for a status bar,
   right-click menu on `.c` files, and a formatted report panel.

See [`tools/vscode-utest-extension/README.md`](../vscode-utest-extension/README.md)
for extension install instructions.

## File layout

```
tools/utest_agent/
├── agent.py            Main entry point
├── llm_client.py       Claude / Ollama abstraction layer
├── diff_parser.py      git diff → FileChange list
├── test_mapper.py      src file → test folder mapping
├── coverage_runner.py  cmake / ctest / lcov pipeline
├── coverage_parser.py  lcov .info file parser
├── prompt_builder.py   Prompt assembly for LLM
├── setup_ollama.sh     One-time Ollama install helper
└── requirements.txt    Python dependencies
```

## Test folder mapping

The agent follows cliforge's naming convention:

| Source file | Test folder |
|-------------|-------------|
| `src/cf_lex.c` | `tests/unit-tests/ut_cf_lex/` |
| `src/cf_parse.c` | `tests/unit-tests/ut_cf_parse/` |
| `src/cf_gen.c` | `tests/unit-tests/ut_cf_gen/` |
| `src/cf_util.c` | `tests/unit-tests/ut_cf_util/` |

If a test folder is missing entirely, the agent flags it as a gap.
