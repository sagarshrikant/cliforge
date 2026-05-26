"""
llm_client.py
-------------
Abstraction layer that makes Claude API and Ollama interchangeable.

Think of this like a power adapter — same plug shape (the interface),
different voltage behind it (Claude cloud vs Ollama local).
The rest of the agent only talks to LLMClient and never knows which
backend is running.

Usage:
    client = make_client("claude")          # uses ANTHROPIC_API_KEY
    client = make_client("ollama")          # uses localhost:11434
    client = make_client("ollama", model="qwen2.5-coder:7b")

    for chunk in client.stream(system_prompt, user_prompt):
        print(chunk, end="", flush=True)

    full_text = client.complete(system_prompt, user_prompt)
"""

import os
import sys
import json
import time
from abc import ABC, abstractmethod
from typing import Iterator


# ── Recommended Ollama models for C/GTest analysis ───────────────
# Ranked by: code quality > speed > RAM requirement
OLLAMA_RECOMMENDED_MODELS = [
    ("qwen2.5-coder:7b",      "Best balance: great C code quality, ~5GB RAM, fast enough"),
    ("qwen2.5-coder:14b",     "Better quality, ~9GB RAM, noticeably slower"),
    ("deepseek-coder-v2:16b", "Excellent code, ~10GB RAM, needs decent GPU"),
    ("codellama:13b",         "Good for C specifically, ~8GB RAM"),
    ("llama3.1:8b",           "General purpose fallback, ~5GB RAM"),
    ("mistral:7b",            "Decent, faster than llama3, ~4GB RAM"),
]

DEFAULT_OLLAMA_MODEL = "qwen2.5-coder:7b"
DEFAULT_CLAUDE_MODEL = "claude-sonnet-4-20250514"

# Ollama context window per model (tokens) — used for truncation decisions
OLLAMA_CONTEXT_LIMITS = {
    "qwen2.5-coder:7b":      32768,
    "qwen2.5-coder:14b":     32768,
    "deepseek-coder-v2:16b": 65536,
    "codellama:13b":          16384,
    "llama3.1:8b":             8192,
    "mistral:7b":              8192,
}


class LLMClient(ABC):
    """Abstract base — every backend must implement these two methods."""

    @abstractmethod
    def stream(self, system_prompt: str, user_prompt: str) -> Iterator[str]:
        """Yield response text chunks as they arrive."""
        ...

    @abstractmethod
    def complete(self, system_prompt: str, user_prompt: str) -> str:
        """Return full response text (blocking)."""
        ...

    @property
    @abstractmethod
    def name(self) -> str:
        """Human-readable backend name, e.g. 'Claude (claude-sonnet-4-...)' """
        ...

    @property
    def context_limit_chars(self) -> int:
        """Approximate safe character limit for prompts (not tokens)."""
        return 80000  # default ~20k tokens worth


# ══════════════════════════════════════════════════════════════════
# Claude backend
# ══════════════════════════════════════════════════════════════════

class ClaudeClient(LLMClient):
    """
    Anthropic Claude API client.
    Requires: pip install anthropic
              ANTHROPIC_API_KEY environment variable
    """

    def __init__(self, model: str = DEFAULT_CLAUDE_MODEL, max_tokens: int = 4096):
        try:
            import anthropic as _anthropic
        except ImportError:
            print("❌ anthropic package not installed. Run: pip install anthropic")
            sys.exit(1)

        api_key = os.environ.get("ANTHROPIC_API_KEY")
        if not api_key:
            print("❌ ANTHROPIC_API_KEY environment variable not set.")
            print("   Get a key at https://console.anthropic.com")
            sys.exit(1)

        self._client = _anthropic.Anthropic(api_key=api_key)
        self._model = model
        self._max_tokens = max_tokens

    @property
    def name(self) -> str:
        return f"Claude ({self._model})"

    @property
    def context_limit_chars(self) -> int:
        return 600000  # Claude Sonnet supports ~200k tokens ≈ 800k chars; use 600k to be safe

    def stream(self, system_prompt: str, user_prompt: str) -> Iterator[str]:
        with self._client.messages.stream(
            model=self._model,
            max_tokens=self._max_tokens,
            system=system_prompt,
            messages=[{"role": "user", "content": user_prompt}]
        ) as stream:
            yield from stream.text_stream

    def complete(self, system_prompt: str, user_prompt: str) -> str:
        response = self._client.messages.create(
            model=self._model,
            max_tokens=self._max_tokens,
            system=system_prompt,
            messages=[{"role": "user", "content": user_prompt}]
        )
        return response.content[0].text


# ══════════════════════════════════════════════════════════════════
# Ollama backend
# ══════════════════════════════════════════════════════════════════

class OllamaClient(LLMClient):
    """
    Local Ollama LLM client.
    Requires: Ollama installed and running (ollama serve)
              Model pulled: ollama pull qwen2.5-coder:7b
    Uses Ollama's native /api/chat endpoint (no extra packages needed — just requests).
    """

    def __init__(
        self,
        model: str = DEFAULT_OLLAMA_MODEL,
        base_url: str = "http://localhost:11434",
        max_tokens: int = 2048,          # lower default — local models are slower
        timeout_sec: int = 300,          # 5 min — local models are SLOW for large prompts
        temperature: float = 0.1,        # low temp = more deterministic for code tasks
    ):
        try:
            import requests as _requests
        except ImportError:
            print("❌ requests package not installed. Run: pip install requests")
            sys.exit(1)

        self._requests = _requests
        self._model = model
        self._base_url = base_url.rstrip("/")
        self._max_tokens = max_tokens
        self._timeout = timeout_sec
        self._temperature = temperature

        # Verify Ollama is reachable and model is available
        self._check_ollama()

    @property
    def name(self) -> str:
        return f"Ollama ({self._model} @ {self._base_url})"

    @property
    def context_limit_chars(self) -> int:
        """Conservative limit based on model's known context window."""
        token_limit = OLLAMA_CONTEXT_LIMITS.get(self._model, 4096)
        # ~3 chars per token on average for code/C source
        return int(token_limit * 3 * 0.7)  # 70% of theoretical max to leave room for response

    def stream(self, system_prompt: str, user_prompt: str) -> Iterator[str]:
        """
        Stream response from Ollama /api/chat endpoint.
        Each line is a JSON object with a 'message.content' delta.
        """
        url = f"{self._base_url}/api/chat"
        payload = {
            "model": self._model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user",   "content": user_prompt},
            ],
            "stream": True,
            "options": {
                "temperature": self._temperature,
                "num_predict": self._max_tokens,
            }
        }

        try:
            response = self._requests.post(
                url, json=payload,
                stream=True,
                timeout=self._timeout
            )
            response.raise_for_status()

            for raw_line in response.iter_lines():
                if not raw_line:
                    continue
                try:
                    chunk = json.loads(raw_line)
                    delta = chunk.get("message", {}).get("content", "")
                    if delta:
                        yield delta
                    if chunk.get("done"):
                        break
                except json.JSONDecodeError:
                    continue

        except self._requests.exceptions.ConnectionError:
            yield "\n\n❌ Cannot connect to Ollama. Is it running? Run: ollama serve\n"
        except self._requests.exceptions.Timeout:
            yield f"\n\n⚠️  Ollama timed out after {self._timeout}s. Try a smaller model or shorter prompt.\n"
        except self._requests.exceptions.HTTPError as e:
            if e.response.status_code == 404:
                yield f"\n\n❌ Model '{self._model}' not found. Run: ollama pull {self._model}\n"
            else:
                yield f"\n\n❌ Ollama HTTP error: {e}\n"

    def complete(self, system_prompt: str, user_prompt: str) -> str:
        """Non-streaming version — collects full response."""
        return "".join(self.stream(system_prompt, user_prompt))

    def _check_ollama(self):
        """Check Ollama is running and the requested model is available."""
        try:
            resp = self._requests.get(f"{self._base_url}/api/tags", timeout=5)
            resp.raise_for_status()
            available = [m["name"] for m in resp.json().get("models", [])]

            # Normalize: "qwen2.5-coder:7b" may be stored as "qwen2.5-coder:7b" or similar
            model_base = self._model.split(":")[0]
            found = any(model_base in m for m in available)

            if not found:
                print(f"⚠️  Model '{self._model}' not found locally.")
                print(f"   Available: {', '.join(available) or '(none)'}")
                print(f"   Pull it with: ollama pull {self._model}")
                print(f"   Recommended models:")
                for model, desc in OLLAMA_RECOMMENDED_MODELS[:4]:
                    print(f"     ollama pull {model}  ← {desc}")
                sys.exit(1)

        except self._requests.exceptions.ConnectionError:
            print(f"❌ Ollama not running at {self._base_url}")
            print("   Start it with: ollama serve")
            print("   Install from:  https://ollama.com")
            sys.exit(1)


# ══════════════════════════════════════════════════════════════════
# Factory
# ══════════════════════════════════════════════════════════════════

def make_client(
    backend: str = "claude",
    model: str = None,
    ollama_url: str = "http://localhost:11434",
    max_tokens: int = None,
) -> LLMClient:
    """
    Factory function — create the right client based on backend name.

    Args:
        backend:    "claude" or "ollama"
        model:      Override the default model for either backend
        ollama_url: Ollama server URL (default: localhost)
        max_tokens: Override default max output tokens

    Returns:
        LLMClient instance ready to use
    """
    backend = backend.lower().strip()

    if backend == "claude":
        return ClaudeClient(
            model=model or DEFAULT_CLAUDE_MODEL,
            max_tokens=max_tokens or 4096,
        )
    elif backend in ("ollama", "local"):
        return OllamaClient(
            model=model or DEFAULT_OLLAMA_MODEL,
            base_url=ollama_url,
            max_tokens=max_tokens or 2048,
        )
    else:
        print(f"❌ Unknown backend: '{backend}'. Use 'claude' or 'ollama'.")
        sys.exit(1)


def truncate_prompt_for_backend(prompt: str, client: LLMClient) -> str:
    """
    Truncate a prompt to fit the backend's context window.
    Claude: essentially no truncation needed (200k tokens).
    Ollama: aggressive truncation needed for smaller models.

    Strategy: keep the beginning (file structure + diffs) and
    end (task instructions), drop the middle (verbose src content).
    """
    limit = client.context_limit_chars
    if len(prompt) <= limit:
        return prompt

    # Split at the midpoint and keep first 60% + last 20%
    keep_start = int(limit * 0.65)
    keep_end = int(limit * 0.25)
    truncated_chars = len(prompt) - keep_start - keep_end

    truncation_notice = (
        f"\n\n[... {truncated_chars:,} characters of source/diff content truncated "
        f"to fit {type(client).__name__} context window of ~{limit:,} chars ...]\n\n"
    )

    return prompt[:keep_start] + truncation_notice + prompt[-keep_end:]


def print_backend_info(client: LLMClient):
    """Print backend info at startup so user knows which LLM is running."""
    is_ollama = isinstance(client, OllamaClient)
    icon = "🏠" if is_ollama else "☁️ "
    print(f"{icon} LLM backend: {client.name}")
    if is_ollama:
        print(f"   Context limit: ~{client.context_limit_chars:,} chars "
              f"(prompt will be truncated if larger)")
        print(f"   Tip: Expect 30s–5min response time depending on hardware")
