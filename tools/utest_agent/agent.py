"""
agent.py — UTest Agent (v3 — dual LLM: Claude API + Ollama local)
==================================================================
Full pipeline:

  [optional] cmake build  →  ctest  →  lcov capture + parse
      ↓
  git diff → parse changed functions → map to test folders
      ↓
  build enriched prompt (diff + coverage gaps + build results)
      ↓
  Claude API  OR  Ollama (local)  → streaming report
      ↓
  PR readiness verdict

Usage:
    python agent.py                              # Claude, diff-only (fast)
    python agent.py --llm ollama                 # Ollama local, diff-only
    python agent.py --llm ollama --model qwen2.5-coder:7b --with-build
    python agent.py --with-build                 # full: build+test+coverage+AI (Claude)
    python agent.py --coverage-only              # re-parse last lcov, no rebuild
    python agent.py --prepr                      # pre-PR gate (exits 1 if not ready)
    python agent.py --file src/app.c             # single file
    python agent.py --scan src/safety.c          # full coverage scan
    python agent.py --output report.md           # save to file
    python agent.py --list-models                # show recommended Ollama models
"""

import argparse
import subprocess
import sys
import os
from pathlib import Path

from diff_parser import get_changed_files, get_changed_files_from_path
from test_mapper import src_to_test_folder, scan_test_folder, read_src_file_functions
from prompt_builder import (build_prompt, build_full_scan_prompt,
                            inject_coverage_section, SYSTEM_PROMPT)
from coverage_runner import run_cmake_build, run_ctest, run_lcov, reset_coverage_counters
from coverage_parser import parse_lcov_info, get_gaps_for_src_file
from llm_client import (make_client, truncate_prompt_for_backend,
                        print_backend_info, OLLAMA_RECOMMENDED_MODELS)

# ── Defaults ──────────────────────────────────────────────────────
SRC_DIR          = "src"
TESTS_ROOT       = "tests/unit-test"
DEFAULT_BUILD    = "build"
DEFAULT_COV_DIR  = "coverage_report"


def main():
    parser = argparse.ArgumentParser(
        description="UTest Agent — AI-powered GTest + lcov coverage advisor",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python agent.py                                  Quick diff-only (Claude)
  python agent.py --llm ollama                     Quick diff-only (Ollama local)
  python agent.py --llm ollama --model codellama:13b
  python agent.py --with-build                     Full pipeline (Claude)
  python agent.py --llm ollama --with-build        Full pipeline (Ollama)
  python agent.py --coverage-only                  Re-parse last coverage, no rebuild
  python agent.py --prepr                          Pre-PR gate
  python agent.py --file src/app.c                 Single file
  python agent.py --scan src/safety.c              Full coverage scan
  python agent.py --list-models                    Show recommended Ollama models
        """
    )

    # ── LLM backend ──────────────────────────────────────────────
    llm_group = parser.add_argument_group("LLM backend")
    llm_group.add_argument(
        "--llm", default="claude", choices=["claude", "ollama"],
        help="LLM backend to use (default: claude)"
    )
    llm_group.add_argument(
        "--model", default=None,
        help="Override default model (e.g. qwen2.5-coder:7b for Ollama, "
             "claude-opus-4-5 for Claude)"
    )
    llm_group.add_argument(
        "--ollama-url", default="http://localhost:11434",
        help="Ollama server URL (default: http://localhost:11434)"
    )
    llm_group.add_argument(
        "--list-models", action="store_true",
        help="List recommended Ollama models and exit"
    )

    # ── Pipeline modes ────────────────────────────────────────────
    mode_group = parser.add_argument_group("Pipeline modes")
    mode_group.add_argument("--with-build", action="store_true",
        help="cmake build + ctest + lcov before AI analysis")
    mode_group.add_argument("--coverage-only", action="store_true",
        help="Re-parse existing coverage.info without rebuilding")
    mode_group.add_argument("--prepr", action="store_true",
        help="Pre-PR gate: build+test+coverage, exit 1 if not ready")
    mode_group.add_argument("--scan", metavar="SRC_FILE",
        help="Full coverage scan for a specific src file")
    mode_group.add_argument("--file", metavar="SRC_FILE",
        help="Analyze a specific src file diff only")

    # ── Build options ─────────────────────────────────────────────
    build_group = parser.add_argument_group("Build options")
    build_group.add_argument("--build-dir", default=DEFAULT_BUILD,
        help=f"CMake build directory (default: {DEFAULT_BUILD})")
    build_group.add_argument("--coverage-dir", default=DEFAULT_COV_DIR,
        help=f"Coverage output directory (default: {DEFAULT_COV_DIR})")
    build_group.add_argument("--no-html", action="store_true",
        help="Skip genhtml HTML report generation")
    build_group.add_argument("--jobs", type=int, default=4,
        help="Parallel build jobs (default: 4)")
    build_group.add_argument("--test-filter", metavar="REGEX",
        help="CTest filter regex (e.g. 'ut_safety')")

    # ── Diff options ──────────────────────────────────────────────
    diff_group = parser.add_argument_group("Diff options")
    diff_group.add_argument("--base", default="HEAD",
        help="Git ref to diff against (default: HEAD)")
    diff_group.add_argument("--src-dir", default=SRC_DIR)
    diff_group.add_argument("--tests-root", default=TESTS_ROOT)

    # ── Output ────────────────────────────────────────────────────
    out_group = parser.add_argument_group("Output")
    out_group.add_argument("--output", metavar="FILE",
        help="Write report to file (also printed to stdout)")
    out_group.add_argument("--no-ai", action="store_true",
        help="Print coverage gaps only, skip AI analysis")

    args = parser.parse_args()

    # ── --list-models ──────────────────────────────────────────────
    if args.list_models:
        _print_recommended_models()
        return

    # ── Validate git repo ──────────────────────────────────────────
    _check_git_repo()

    # ── Create LLM client ─────────────────────────────────────────
    client = None
    if not args.no_ai:
        client = make_client(
            backend=args.llm,
            model=args.model,
            ollama_url=args.ollama_url,
        )
        print_backend_info(client)

    # ──────────────────────────────────────────────────────────────
    # Build / Test / Coverage pipeline
    # ──────────────────────────────────────────────────────────────
    run_build    = args.with_build or args.prepr
    use_coverage = run_build or args.coverage_only

    build_result = ctest_result = lcov_result = coverage_report = None

    if run_build:
        print("\n" + "═"*60)
        print(" STEP 1/3 — CMake Build")
        print("═"*60)
        build_result = run_cmake_build(build_dir=args.build_dir, jobs=args.jobs)
        if not build_result.success:
            print("\n❌ BUILD FAILED")
            print(build_result.stderr[:2000])
            if args.prepr:
                _prepr_verdict(False, ["Build failed — fix compile errors first"])
            sys.exit(1)

        print("\n" + "═"*60)
        print(" STEP 2/3 — CTest")
        print("═"*60)
        reset_coverage_counters(args.build_dir)
        ctest_result = run_ctest(build_dir=args.build_dir, test_filter=args.test_filter)

        print("\n" + "═"*60)
        print(" STEP 3/3 — lcov Coverage")
        print("═"*60)
        lcov_result = run_lcov(
            build_dir=args.build_dir,
            output_dir=args.coverage_dir,
            generate_html=not args.no_html,
            src_dir=args.src_dir,
        )

    elif args.coverage_only:
        info_file = str(Path(args.coverage_dir) / "coverage_filtered.info")
        if not Path(info_file).exists():
            info_file = str(Path(args.coverage_dir) / "coverage.info")
        if not Path(info_file).exists():
            print(f"❌ No coverage.info in '{args.coverage_dir}/'. Run --with-build first.")
            sys.exit(1)
        print(f"📊 Reusing coverage data: {info_file}")
        lcov_result = _fake_lcov_result(info_file)

    if use_coverage and lcov_result and lcov_result.success:
        info_path = getattr(lcov_result, "filtered_info_file", None) or lcov_result.info_file
        coverage_report = parse_lcov_info(info_path, project_root=".")
        _print_coverage_summary(coverage_report)

    # ──────────────────────────────────────────────────────────────
    # Mode: full coverage scan for one file
    # ──────────────────────────────────────────────────────────────
    if args.scan:
        src_file = args.scan
        content  = Path(src_file).read_text(errors="replace") if Path(src_file).exists() else ""
        all_funcs = read_src_file_functions(src_file)
        tf_path   = src_to_test_folder(src_file, args.tests_root)
        tf        = scan_test_folder(tf_path, src_file)
        print(f"\n🔍 Full scan: {src_file} — {len(all_funcs)} functions, {len(tf.existing_tests)} test files")
        prompt = build_full_scan_prompt(src_file, content, tf, all_funcs)
        if coverage_report:
            gap = get_gaps_for_src_file(coverage_report, src_file)
            if gap:
                prompt += f"\n\n## lcov Coverage\n{gap.coverage_summary}"
        if not args.no_ai:
            prompt = truncate_prompt_for_backend(prompt, client)
            report = _stream_llm(client, prompt)
            _save_report(report, args.output)
        return

    # ──────────────────────────────────────────────────────────────
    # Mode: diff-based analysis
    # ──────────────────────────────────────────────────────────────
    if args.file:
        file_changes = get_changed_files_from_path(args.file)
    else:
        file_changes = get_changed_files(base_ref=args.base, src_dir=args.src_dir)

    if not file_changes and not use_coverage:
        print("✅ No changes detected and no coverage data. Nothing to report.")
        return

    if file_changes:
        print(f"\n📂 Source changes: {len(file_changes)} file(s)")
        for fc in file_changes:
            print(f"   {fc.status:14s}  {fc.filepath}")
            for fn in fc.functions:
                print(f"                  → {fn.change_type}: {fn.name}()")

    # If no diff but we have coverage, surface all gap files
    files_to_analyze = file_changes
    if not files_to_analyze and coverage_report:
        from diff_parser import FileChange as FC
        files_to_analyze = [
            FC(filepath=gap.src_file, status="coverage_gap")
            for gap in coverage_report.files_with_gaps
        ]

    test_folders = {}
    src_contents = {}
    diff_texts   = {}
    changed_paths = [fc.filepath for fc in file_changes]

    for fc in files_to_analyze:
        sp = fc.filepath
        if Path(sp).exists():
            src_contents[sp] = Path(sp).read_text(errors="replace")
        try:
            diff = subprocess.check_output(["git", "diff", "HEAD", "--", sp], text=True)
            if not diff:
                diff = subprocess.check_output(["git", "diff", "--cached", "--", sp], text=True)
        except subprocess.CalledProcessError:
            diff = ""
        diff_texts[sp]  = diff
        test_folders[sp] = scan_test_folder(src_to_test_folder(sp, args.tests_root), sp)

    # Build prompt
    if files_to_analyze:
        prompt = build_prompt(files_to_analyze, test_folders, src_contents, diff_texts)
    else:
        prompt = "## Coverage-only analysis\nNo source files changed. Analyze coverage gaps.\n"

    if coverage_report or ctest_result or build_result:
        prompt = inject_coverage_section(
            prompt,
            coverage_report=coverage_report,
            ctest_result=ctest_result,
            build_result=build_result,
            changed_files=changed_paths,
        )

    # ── AI call ──────────────────────────────────────────────────
    print("\n" + "═"*60)
    print(" AI ANALYSIS")
    print("═"*60 + "\n")

    if args.no_ai:
        print("[--no-ai: skipping LLM analysis]")
        _save_report("", args.output)
        return

    prompt = truncate_prompt_for_backend(prompt, client)
    report = _stream_llm(client, prompt)
    _save_report(report, args.output)

    # Pre-PR verdict
    if args.prepr:
        blocking = _collect_blocking_issues(build_result, ctest_result, coverage_report)
        _prepr_verdict(not blocking, blocking)
        sys.exit(1 if blocking else 0)


# ──────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────

def _stream_llm(client, user_prompt: str) -> str:
    """Stream LLM response to stdout and return full text."""
    chunks = []
    for chunk in client.stream(SYSTEM_PROMPT, user_prompt):
        print(chunk, end="", flush=True)
        chunks.append(chunk)
    print()
    return "".join(chunks)


def _print_coverage_summary(report):
    if not report or not report.files:
        return
    print(f"\n📊 Coverage Summary")
    print(f"   Lines:     {report.total_line_pct:.1f}%")
    print(f"   Functions: {report.total_function_pct:.1f}%")
    print(f"   Branches:  {report.total_branch_pct:.1f}%")
    gaps = report.files_with_gaps
    if gaps:
        print(f"   Files with gaps: {len(gaps)}")
        for g in gaps:
            print(f"     ⚠  {g.src_file}  "
                  f"funcs {g.function_coverage_pct:.0f}%  "
                  f"lines {g.line_coverage_pct:.0f}%  "
                  f"branches {g.branch_coverage_pct:.0f}%")
    else:
        print("   ✅ 100% on all src files!")


def _collect_blocking_issues(build_result, ctest_result, coverage_report) -> list[str]:
    issues = []
    if build_result and not build_result.success:
        issues.append("Build failed")
    if ctest_result and not ctest_result.success:
        failed = [t.name for t in ctest_result.test_cases if t.status == "FAILED"]
        issues.append(f"{len(failed)} test(s) failing: {', '.join(failed[:5])}")
    if coverage_report:
        if coverage_report.total_function_pct < 100.0:
            n = sum(len(g.uncovered_functions) for g in coverage_report.files_with_gaps)
            issues.append(f"{n} uncovered function(s)")
        if coverage_report.total_line_pct < 100.0:
            issues.append(f"Line coverage: {coverage_report.total_line_pct:.1f}% < 100%")
    return issues


def _prepr_verdict(ready: bool, blocking: list[str]):
    print("\n" + "═"*60)
    if ready:
        print("✅  PR READY — build green, all tests pass, 100% coverage")
    else:
        print("❌  PR BLOCKED — fix before submitting:")
        for b in blocking:
            print(f"   • {b}")
    print("═"*60)


def _save_report(report: str, output_file: str | None):
    if output_file and report:
        Path(output_file).write_text(report)
        print(f"\n💾 Saved: {output_file}")


def _check_git_repo():
    try:
        subprocess.check_output(["git", "rev-parse", "--show-toplevel"],
                                 stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("❌ Not inside a git repository.")
        sys.exit(1)


def _print_recommended_models():
    print("\nRecommended Ollama models for C/GTest analysis:\n")
    print(f"  {'Model':<30}  {'Notes'}")
    print("  " + "-"*70)
    for model, desc in OLLAMA_RECOMMENDED_MODELS:
        print(f"  {model:<30}  {desc}")
    print()
    print("Install Ollama:  https://ollama.com")
    print("Pull a model:    ollama pull qwen2.5-coder:7b")
    print("Start server:    ollama serve")
    print()
    print("Usage:")
    print("  python agent.py --llm ollama --model qwen2.5-coder:7b --with-build")


class _fake_lcov_result:
    """Minimal duck-typed object for --coverage-only mode."""
    def __init__(self, info_file: str):
        self.success = True
        self.info_file = info_file
        self.filtered_info_file = info_file
        self.html_dir = ""


if __name__ == "__main__":
    main()
