"""
prompt_builder.py
-----------------
Constructs the prompts sent to the LLM.

Think of this as the "briefing document" writer — it takes raw facts
(git diff, test folder contents, coverage gaps, build results) and
assembles them into a focused prompt that gives the AI exactly the
context it needs, nothing more.

Two main prompts:
  build_prompt()          — diff-driven: "here is what changed, what tests do we need?"
  build_full_scan_prompt() — scan-driven: "here is the whole file, cover every function"

Plus inject_coverage_section() which bolts coverage data onto either prompt.
"""

from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from diff_parser import FileChange
    from test_mapper import TestFolder
    from coverage_parser import CoverageReport
    from coverage_runner import CTestResult, BuildResult


# ──────────────────────────────────────────────────────────────────
# System prompt — defines the AI's role
# ──────────────────────────────────────────────────────────────────

SYSTEM_PROMPT = """\
You are an expert C/C++ test engineer specialising in GTest unit tests and lcov coverage
for embedded and systems C projects. The project you are reviewing is **cliforge** — a
command-line parser generator that produces MISRA-C-compatible C89/C99/C11 source from
declarative .cf schema files.

Your responsibilities:
1. Review git diffs and coverage reports to find under-tested code.
2. Suggest concrete GTest test cases (TEST / TEST_F) with real assertions, not pseudocode.
3. Map each suggestion to the correct test file path under tests/unit-tests/ut_<module>/.
4. Prioritise: error paths, boundary conditions, MISRA-unsafe patterns (pointer arithmetic,
   unchecked returns, integer overflow).
5. For positional and compound-type parsing code, include round-trip tests
   (parse → regenerate → compare).
6. Keep suggestions actionable: show the skeleton TEST block with the right includes.
7. When a function has 0% coverage, always suggest at least one positive and one negative test.

Format your response as Markdown:
  ## Summary
  ## Test gaps found
  ## Suggested test cases (with code)
  ## Coverage verdict
"""


# ──────────────────────────────────────────────────────────────────
# Main prompt builders
# ──────────────────────────────────────────────────────────────────

def build_prompt(
    file_changes,          # list[FileChange]
    test_folders: dict,    # {src_file: TestFolder}
    src_contents: dict,    # {src_file: str}
    diff_texts: dict,      # {src_file: str}
) -> str:
    """
    Build the diff-driven prompt.
    One section per changed file: diff, existing tests, and src content.
    """
    parts: list[str] = [
        "# UTest Agent — Diff Analysis\n",
        f"Changed files: {len(file_changes)}\n",
    ]

    for fc in file_changes:
        sp = fc.filepath
        tf = test_folders.get(sp)

        parts.append(f"\n{'='*70}")
        parts.append(f"## File: {sp}  [{fc.status}]")

        # Changed functions
        if fc.functions:
            parts.append(f"\n### Changed functions ({len(fc.functions)})")
            for fn in fc.functions:
                parts.append(f"  - {fn.change_type}: `{fn.name}()`")

        # Existing test folder
        if tf:
            if tf.exists:
                parts.append(f"\n### Existing test folder: {tf.folder_path}")
                if tf.all_test_names:
                    parts.append(f"Tests found ({len(tf.all_test_names)}):")
                    for name in tf.all_test_names[:40]:     # cap for prompt size
                        parts.append(f"  - {name}")
                    if len(tf.all_test_names) > 40:
                        parts.append(f"  ... and {len(tf.all_test_names) - 40} more")
                else:
                    parts.append("⚠️  Test folder exists but contains NO test cases.")
            else:
                parts.append(f"\n### ⚠️  NO test folder found at: {tf.folder_path}")
                parts.append("This src file has no unit tests at all.")

        # Diff text
        diff = diff_texts.get(sp, "")
        if diff:
            parts.append(f"\n### Diff")
            parts.append("```diff")
            parts.append(diff[:8000])   # cap large diffs
            if len(diff) > 8000:
                parts.append(f"... [{len(diff) - 8000} more bytes truncated]")
            parts.append("```")

        # Source content (trimmed)
        src = src_contents.get(sp, "")
        if src:
            parts.append(f"\n### Source ({sp})")
            parts.append("```c")
            parts.append(src[:12000])
            if len(src) > 12000:
                parts.append(f"... [{len(src) - 12000} more bytes truncated]")
            parts.append("```")

    parts.append(f"\n{'='*70}")
    parts.append(
        "\n## Task\n"
        "Review the diffs and existing tests above. For each changed function:\n"
        "1. Identify which GTest cases are missing or need updating.\n"
        "2. Write the missing test skeletons (full TEST / TEST_F blocks).\n"
        "3. Specify the exact file path where each test should be added.\n"
        "4. Note any existing tests that should be deleted or renamed due to refactoring.\n"
        "5. Give a PR-readiness verdict at the end.\n"
    )

    return "\n".join(parts)


def build_full_scan_prompt(
    src_file: str,
    content: str,
    test_folder,        # TestFolder
    all_functions: list[str],
) -> str:
    """
    Build the full-scan prompt: covers every function in the file,
    not just the diff hunks.
    """
    parts = [
        "# UTest Agent — Full Coverage Scan\n",
        f"## Source file: {src_file}",
        f"Total functions found: {len(all_functions)}",
        "",
    ]

    if all_functions:
        parts.append("### All functions:")
        for fn in all_functions:
            parts.append(f"  - `{fn}()`")

    if test_folder.exists:
        parts.append(f"\n### Existing tests in {test_folder.folder_path}:")
        if test_folder.all_test_names:
            for name in test_folder.all_test_names:
                parts.append(f"  - {name}")
        else:
            parts.append("  (no tests yet)")
    else:
        parts.append(f"\n### ⚠️  No test folder at {test_folder.folder_path}")

    parts.append(f"\n### Source")
    parts.append("```c")
    parts.append(content[:20000])
    if len(content) > 20000:
        parts.append(f"... [{len(content)-20000} more bytes truncated]")
    parts.append("```")

    parts.append(
        "\n## Task\n"
        "For EVERY function listed above:\n"
        "1. Check whether a test exists in the test folder.\n"
        "2. If missing, write a complete GTest TEST or TEST_F block.\n"
        "3. Cover: normal path, error/NULL path, boundary values.\n"
        "4. Specify the exact file name and path for each new test.\n"
        "5. Summarise coverage verdict (which functions are fully covered).\n"
    )

    return "\n".join(parts)


def inject_coverage_section(
    prompt: str,
    coverage_report=None,    # CoverageReport | None
    ctest_result=None,       # CTestResult | None
    build_result=None,       # BuildResult | None
    changed_files=None,      # list[str] | None
) -> str:
    """
    Append a ## Coverage Data section to an existing prompt.
    Only includes data for changed files (or all gap files if no diff).
    """
    sections: list[str] = ["\n\n## Coverage & Test Run Data\n"]

    # Build result summary
    if build_result is not None:
        icon = "✅" if build_result.success else "❌"
        sections.append(f"**Build:** {icon} {'OK' if build_result.success else 'FAILED'}")
        if not build_result.success and build_result.stderr:
            sections.append("```")
            sections.append(build_result.stderr[:1500])
            sections.append("```")

    # CTest summary
    if ctest_result is not None:
        icon = "✅" if ctest_result.success else "❌"
        sections.append(
            f"**Tests:** {icon} {ctest_result.passed}/{ctest_result.total} passed"
        )
        failed = [t for t in ctest_result.test_cases if t.status == "FAILED"]
        if failed:
            sections.append(f"Failing tests ({len(failed)}):")
            for t in failed:
                sections.append(f"  - {t.name}")

    # Per-file coverage
    if coverage_report is not None:
        sections.append(
            f"\n**Overall coverage:** "
            f"lines {coverage_report.total_line_pct:.1f}%  "
            f"functions {coverage_report.total_function_pct:.1f}%  "
            f"branches {coverage_report.total_branch_pct:.1f}%"
        )

        # Filter to files relevant to this diff
        relevant: list = []
        if changed_files:
            from coverage_parser import get_gaps_for_src_file
            for cf in changed_files:
                fc = get_gaps_for_src_file(coverage_report, cf)
                if fc:
                    relevant.append(fc)
        else:
            relevant = coverage_report.files_with_gaps

        if relevant:
            sections.append("\n**Per-file coverage gaps:**")
            for fc in relevant:
                sections.append(f"\n{fc.coverage_summary}")
        else:
            sections.append("\n✅ 100% coverage on all changed files!")

    return prompt + "\n".join(sections)
