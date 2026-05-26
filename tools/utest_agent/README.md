# UTest Agent

AI-powered unit test advisor for cliforge. Analyses `git diff` output
and lcov coverage data then suggests GTest additions, removals, and
updates — using either the Claude API (cloud) or a local Ollama model.

## Quick start

```bash
# Install Python dependencies (once)
pip install -r tools/utest_agent/requirements.txt

# Set Claude API key (if using Claude backend)
export ANTHROPIC_API_KEY=sk-ant-...

# Fast path: analyse only what changed since HEAD
python tools/utest_agent/agent.py

# Full pipeline: build → test → coverage → AI suggestions
python tools/utest_agent/agent.py --with-build

# Offline (no API key needed): use local Ollama
python tools/utest_agent/agent.py --llm ollama --with-build
```

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
