"""
diff_parser.py
--------------
Parses `git diff` output to find changed C source files and which functions
inside them were added, modified, or deleted.

Think of it as a "what changed?" sensor — it answers exactly two questions:
  1. Which .c files in src/ were touched?
  2. Inside each file, which function signatures changed?

The agent then uses this list to decide which GTest folders to look at.
"""

import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class FunctionChange:
    """One function that was touched in the diff."""
    name: str
    change_type: str   # "modified" | "added" | "deleted"


@dataclass
class FileChange:
    """One .c file that changed, plus the functions touched inside it."""
    filepath: str
    status: str                              # "modified" | "added" | "deleted" | "renamed" | "coverage_gap"
    functions: list = field(default_factory=list)   # list[FunctionChange]


# Matches diff --git a/src/cf_lex.c b/src/cf_lex.c
_DIFF_FILE_RE = re.compile(r"^diff --git a/(.+?) b/(.+?)$")
# Matches @@ -10,7 +10,12 @@ int cf_lex_token(...)
_HUNK_FUNC_RE = re.compile(r"^@@ .+? @@\s*(.*)")
# Matches status line from git diff --name-status: M\tsrc/foo.c
_STATUS_RE     = re.compile(r"^([AMDRT])\d*\t(.+)$")
# C function signature heuristic: return_type identifier( ...
_FUNC_SIG_RE   = re.compile(
    r"^[a-zA-Z_][\w\s\*]+?\b([a-zA-Z_]\w*)\s*\([^;{]*\)\s*(?:\/\*.+?\*\/)?\s*$"
)


def get_changed_files(base_ref: str = "HEAD", src_dir: str = "src") -> list[FileChange]:
    """
    Return FileChange list for all .c files under src_dir that differ
    from base_ref in the working tree (staged + unstaged).
    """
    # Step 1: get a name-status list so we know add/modify/delete per file
    status_map = _get_status_map(base_ref, src_dir)

    if not status_map:
        return []

    # Step 2: get the full diff text for those files
    try:
        diff_text = subprocess.check_output(
            ["git", "diff", base_ref, "--", *status_map.keys()],
            text=True, stderr=subprocess.DEVNULL
        )
        if not diff_text:
            diff_text = subprocess.check_output(
                ["git", "diff", "--cached", base_ref, "--", *status_map.keys()],
                text=True, stderr=subprocess.DEVNULL
            )
    except subprocess.CalledProcessError:
        diff_text = ""

    return _parse_diff_text(diff_text, status_map)


def get_changed_files_from_path(src_file: str) -> list[FileChange]:
    """
    Return a single-element list treating the given file as modified,
    extracting functions from the actual HEAD diff for that file.
    """
    if not Path(src_file).exists():
        return [FileChange(filepath=src_file, status="modified")]

    try:
        diff_text = subprocess.check_output(
            ["git", "diff", "HEAD", "--", src_file],
            text=True, stderr=subprocess.DEVNULL
        )
        if not diff_text:
            diff_text = subprocess.check_output(
                ["git", "diff", "--cached", "--", src_file],
                text=True, stderr=subprocess.DEVNULL
            )
    except subprocess.CalledProcessError:
        diff_text = ""

    status_map = {src_file: "modified"}
    result = _parse_diff_text(diff_text, status_map)
    if not result:
        # File exists but no diff — treat all functions as "modified" so we
        # still report coverage gaps
        result = [FileChange(filepath=src_file, status="modified", functions=[])]
    return result


# ──────────────────────────────────────────────────────────────────
# Internal helpers
# ──────────────────────────────────────────────────────────────────

def _get_status_map(base_ref: str, src_dir: str) -> dict[str, str]:
    """Return {filepath: status_letter} for .c files under src_dir."""
    result = {}
    try:
        # Working tree vs base_ref
        out = subprocess.check_output(
            ["git", "diff", "--name-status", base_ref, "--", f"{src_dir}/*.c"],
            text=True, stderr=subprocess.DEVNULL
        )
        if not out:
            out = subprocess.check_output(
                ["git", "diff", "--cached", "--name-status", base_ref, "--", f"{src_dir}/*.c"],
                text=True, stderr=subprocess.DEVNULL
            )
    except subprocess.CalledProcessError:
        return result

    for line in out.splitlines():
        m = _STATUS_RE.match(line.strip())
        if m:
            status_letter, fpath = m.group(1), m.group(2)
            if fpath.endswith(".c"):
                status_map_val = {
                    "A": "added", "M": "modified", "D": "deleted",
                    "R": "renamed", "T": "modified",
                }.get(status_letter, "modified")
                result[fpath] = status_map_val

    return result


def _parse_diff_text(diff_text: str, status_map: dict[str, str]) -> list[FileChange]:
    """Walk the unified diff and collect per-file function changes."""
    changes: dict[str, FileChange] = {}
    current_file: str | None = None
    seen_funcs: dict[str, set] = {}

    for line in diff_text.splitlines():
        # New file boundary
        m = _DIFF_FILE_RE.match(line)
        if m:
            current_file = m.group(2)   # b/ path
            if current_file not in changes:
                status = status_map.get(current_file, "modified")
                changes[current_file] = FileChange(filepath=current_file, status=status)
                seen_funcs[current_file] = set()
            continue

        if current_file is None:
            continue

        # Hunk header — may carry the enclosing function name
        m = _HUNK_FUNC_RE.match(line)
        if m:
            ctx = m.group(1).strip()
            fn = _extract_function_name(ctx)
            if fn and fn not in seen_funcs[current_file]:
                seen_funcs[current_file].add(fn)
                change_type = (
                    "added" if changes[current_file].status == "added" else "modified"
                )
                changes[current_file].functions.append(
                    FunctionChange(name=fn, change_type=change_type)
                )
            continue

        # Added/removed lines that look like function signatures
        if line.startswith(("+", "-")) and not line.startswith(("+++", "---")):
            src_line = line[1:].strip()
            fn = _extract_function_name(src_line)
            if fn and fn not in seen_funcs.get(current_file, set()):
                seen_funcs[current_file].add(fn)
                change_type = "added" if line.startswith("+") else "deleted"
                changes[current_file].functions.append(
                    FunctionChange(name=fn, change_type=change_type)
                )

    return list(changes.values())


def _extract_function_name(text: str) -> str | None:
    """
    Very conservative heuristic: only match C function-definition-looking lines.
    Returns the function name or None.
    """
    text = text.strip()
    # Skip preprocessor, comments, includes, struct/typedef
    if not text or text.startswith(("#", "/*", "*", "//", "typedef", "struct", "}")):
        return None
    m = _FUNC_SIG_RE.match(text)
    if m:
        name = m.group(1)
        # Exclude C keywords that look like identifiers
        if name in {"if", "for", "while", "switch", "return", "else", "do"}:
            return None
        return name
    return None
