#!/bin/bash
# ═══════════════════════════════════════════════════════════════
# setup_ollama.sh — Install Ollama and pull the recommended model
# ═══════════════════════════════════════════════════════════════
# Run once per machine:
#   chmod +x tools/utest_agent/setup_ollama.sh
#   ./tools/utest_agent/setup_ollama.sh
#
# Then use the agent with:
#   python tools/utest_agent/agent.py --llm ollama
# ═══════════════════════════════════════════════════════════════

set -e

# ── Configuration ────────────────────────────────────────────────
# Change this to any model from: python agent.py --list-models
DEFAULT_MODEL="qwen2.5-coder:7b"

# ── Detect OS ────────────────────────────────────────────────────
OS="$(uname -s)"
ARCH="$(uname -m)"

echo "╔══════════════════════════════════════════════════════╗"
echo "║  Ollama Setup for UTest Agent                        ║"
echo "╚══════════════════════════════════════════════════════╝"
echo "  OS:   $OS $ARCH"
echo "  Model: $DEFAULT_MODEL"
echo ""

# ── Install Ollama ───────────────────────────────────────────────
if command -v ollama &>/dev/null; then
    echo "✅ Ollama already installed: $(ollama --version)"
else
    echo "📦 Installing Ollama..."
    if [ "$OS" = "Linux" ]; then
        curl -fsSL https://ollama.com/install.sh | sh
    elif [ "$OS" = "Darwin" ]; then
        if command -v brew &>/dev/null; then
            brew install ollama
        else
            echo "  → Download from https://ollama.com/download/mac"
            echo "  → Or install Homebrew first: https://brew.sh"
            exit 1
        fi
    else
        echo "  → Windows: Download installer from https://ollama.com/download/windows"
        exit 0
    fi
fi

# ── Start Ollama service ─────────────────────────────────────────
echo ""
echo "🚀 Starting Ollama service..."
if pgrep -x "ollama" > /dev/null 2>&1; then
    echo "   Already running."
else
    ollama serve &>/dev/null &
    sleep 3
    echo "   Started."
fi

# ── Check available RAM ──────────────────────────────────────────
echo ""
echo "💾 Checking system RAM..."
if [ "$OS" = "Linux" ]; then
    TOTAL_RAM_GB=$(awk '/MemTotal/ {printf "%.0f", $2/1024/1024}' /proc/meminfo)
elif [ "$OS" = "Darwin" ]; then
    TOTAL_RAM_GB=$(( $(sysctl -n hw.memsize) / 1024 / 1024 / 1024 ))
else
    TOTAL_RAM_GB=0
fi

echo "   Total RAM: ${TOTAL_RAM_GB}GB"
echo ""
echo "   Model size guide:"
echo "   ≥ 4GB  → mistral:7b or llama3.1:8b     (minimum)"
echo "   ≥ 6GB  → qwen2.5-coder:7b               (recommended ⭐)"
echo "   ≥ 10GB → qwen2.5-coder:14b or codellama:13b"
echo "   ≥ 12GB → deepseek-coder-v2:16b           (best quality)"
echo ""

# Auto-suggest based on RAM
if [ "$TOTAL_RAM_GB" -ge 10 ] 2>/dev/null; then
    SUGGESTED="qwen2.5-coder:14b"
elif [ "$TOTAL_RAM_GB" -ge 6 ] 2>/dev/null; then
    SUGGESTED="qwen2.5-coder:7b"
elif [ "$TOTAL_RAM_GB" -ge 4 ] 2>/dev/null; then
    SUGGESTED="mistral:7b"
else
    SUGGESTED="$DEFAULT_MODEL"
fi

if [ "$SUGGESTED" != "$DEFAULT_MODEL" ]; then
    echo "   💡 Based on your RAM, suggested: $SUGGESTED"
    read -r -p "   Use $SUGGESTED instead of $DEFAULT_MODEL? [y/N] " choice
    if [[ "$choice" =~ ^[Yy]$ ]]; then
        DEFAULT_MODEL="$SUGGESTED"
    fi
fi

# ── Pull the model ───────────────────────────────────────────────
echo ""
echo "⬇️  Pulling model: $DEFAULT_MODEL"
echo "   (This downloads ~4-10GB depending on model — one-time only)"
ollama pull "$DEFAULT_MODEL"

# ── Verify ───────────────────────────────────────────────────────
echo ""
echo "✅ Setup complete!"
echo ""
echo "Test it now:"
echo "  python tools/utest_agent/agent.py --llm ollama --model $DEFAULT_MODEL"
echo ""
echo "List available models:"
echo "  ollama list"
echo ""
echo "Note: Ollama runs as a background service."
echo "  Start: ollama serve"
echo "  Stop:  pkill ollama"
