"""
coverage_runner.py
------------------
Runs the cmake build → ctest → lcov pipeline and returns structured results.

Think of the three stages like an assembly line:
    Stage 1 (cmake)  : compile everything, linking errors surface here
    Stage 2 (ctest)  : run the tests, assertion failures surface here
    Stage 3 (lcov)   : measure which lines were actually hit

Each stage returns a typed result object so the agent can give precise
feedback ("build failed at link step" vs "3 tests crashed" vs "60% line coverage").

Requirements on the host machine:
    cmake, make/ninja, gcov, lcov, genhtml
    GTest built with --coverage flags (handled by cliforge's CMakeLists.txt)
"""

import subprocess
import shutil
from dataclasses import dataclass, field
from pathlib import Path


# ──────────────────────────────────────────────────────────────────
# Result types
# ──────────────────────────────────────────────────────────────────

@dataclass
class BuildResult:
    success: bool
    stdout: str = ""
    stderr: str = ""
    returncode: int = 0


@dataclass
class TestCase:
    name: str
    status: str    # "PASSED" | "FAILED" | "SKIPPED" | "TIMEOUT"
    duration_s: float = 0.0


@dataclass
class CTestResult:
    success: bool
    test_cases: list[TestCase] = field(default_factory=list)
    total: int = 0
    passed: int = 0
    failed: int = 0
    stdout: str = ""


@dataclass
class LcovResult:
    success: bool
    info_file: str = ""
    filtered_info_file: str = ""
    html_dir: str = ""
    stderr: str = ""
    line_pct: float = 0.0
    function_pct: float = 0.0
    branch_pct: float = 0.0


# ──────────────────────────────────────────────────────────────────
# Public API
# ──────────────────────────────────────────────────────────────────

def run_cmake_build(build_dir: str = "build", jobs: int = 4) -> BuildResult:
    """
    Run cmake --build on an already-configured build directory.
    If the build dir doesn't exist, configure it first with coverage flags.
    """
    bd = Path(build_dir)

    # Configure if not done yet
    if not (bd / "CMakeCache.txt").exists():
        print(f"  Configuring CMake in '{build_dir}' with coverage flags...")
        cfg_result = _run(
            ["cmake", "-S", ".", "-B", build_dir,
             "-DCMAKE_BUILD_TYPE=Debug",
             "-DCLIFORGE_BUILD_TESTS=ON",
             "-DCMAKE_C_FLAGS=--coverage",
             "-DCMAKE_EXE_LINKER_FLAGS=--coverage"],
        )
        if cfg_result.returncode != 0:
            return BuildResult(
                success=False,
                stdout=cfg_result.stdout,
                stderr=cfg_result.stderr,
                returncode=cfg_result.returncode,
            )

    print(f"  Building with {jobs} job(s)...")
    result = _run(["cmake", "--build", build_dir, "--", f"-j{jobs}"])
    return BuildResult(
        success=result.returncode == 0,
        stdout=result.stdout,
        stderr=result.stderr,
        returncode=result.returncode,
    )


def run_ctest(build_dir: str = "build", test_filter: str | None = None) -> CTestResult:
    """Run ctest inside build_dir and parse the results."""
    cmd = ["ctest", "--test-dir", build_dir, "--output-on-failure", "-V"]
    if test_filter:
        cmd += ["-R", test_filter]

    print(f"  Running ctest{f' (filter: {test_filter})' if test_filter else ''}...")
    result = _run(cmd)
    test_cases = _parse_ctest_output(result.stdout + result.stderr)

    passed = sum(1 for t in test_cases if t.status == "PASSED")
    failed = sum(1 for t in test_cases if t.status == "FAILED")

    return CTestResult(
        success=result.returncode == 0,
        test_cases=test_cases,
        total=len(test_cases),
        passed=passed,
        failed=failed,
        stdout=result.stdout,
    )


def run_lcov(
    build_dir: str = "build",
    output_dir: str = "coverage_report",
    generate_html: bool = True,
    src_dir: str = "src",
) -> LcovResult:
    """
    Capture coverage counters with lcov, filter to src_dir, optionally
    generate an HTML report with genhtml.
    """
    if not shutil.which("lcov"):
        return LcovResult(success=False, stderr="lcov not found — install with: sudo apt install lcov")

    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)
    info_raw      = str(out / "coverage.info")
    info_filtered = str(out / "coverage_filtered.info")
    html_dir      = str(out / "html")

    print("  Capturing coverage counters (lcov)...")
    r = _run(["lcov",
              "--capture",
              "--directory", build_dir,
              "--output-file", info_raw,
              "--rc", "lcov_branch_coverage=1",
              "--quiet"])
    if r.returncode != 0:
        return LcovResult(success=False, stderr=r.stderr)

    # Keep only project source, strip system headers and GTest internals
    print("  Filtering coverage data...")
    r = _run(["lcov",
              "--remove", info_raw,
              "*/googletest*", "*/usr/*", "*/build*", "*/_deps/*", "*/tests/*",
              "--output-file", info_filtered,
              "--rc", "lcov_branch_coverage=1",
              "--quiet"])
    if r.returncode != 0:
        # Fall back to unfiltered
        info_filtered = info_raw

    # Parse headline numbers from lcov --summary
    summary = _run(["lcov", "--summary", info_filtered,
                    "--rc", "lcov_branch_coverage=1"])
    line_pct, fn_pct, br_pct = _parse_lcov_summary(summary.stdout + summary.stderr)

    print(f"  Coverage: lines {line_pct:.1f}%  functions {fn_pct:.1f}%  branches {br_pct:.1f}%")

    # Optional HTML report
    if generate_html and shutil.which("genhtml"):
        print("  Generating HTML report (genhtml)...")
        _run(["genhtml", info_filtered,
              "--output-directory", html_dir,
              "--branch-coverage",
              "--quiet"])
        print(f"  HTML report: {html_dir}/index.html")
    else:
        html_dir = ""

    return LcovResult(
        success=True,
        info_file=info_raw,
        filtered_info_file=info_filtered,
        html_dir=html_dir,
        line_pct=line_pct,
        function_pct=fn_pct,
        branch_pct=br_pct,
    )


def reset_coverage_counters(build_dir: str = "build") -> None:
    """
    Zero all .gcda files before a test run so coverage counts are clean.
    Analogous to clearing the odometer before a test drive.
    """
    if not shutil.which("lcov"):
        return
    _run(["lcov", "--zerocounters", "--directory", build_dir, "--quiet"])


# ──────────────────────────────────────────────────────────────────
# Private helpers
# ──────────────────────────────────────────────────────────────────

def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        cmd, capture_output=True, text=True, timeout=300
    )


def _parse_ctest_output(text: str) -> list[TestCase]:
    """
    Parse ctest -V output for lines like:
        1/5 Test #1: ut_cf_lex_run ..............   Passed    0.01 sec
        2/5 Test #2: ut_cf_parse ...............***Failed    0.05 sec
    """
    import re
    pattern = re.compile(
        r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.+\s*"
        r"(\*+)?(Passed|Failed|Timeout|Skipped)\s+([\d.]+)\s+sec",
        re.IGNORECASE
    )
    cases = []
    for line in text.splitlines():
        m = pattern.search(line)
        if m:
            name = m.group(1)
            raw_status = m.group(3).upper()
            status_map = {"PASSED": "PASSED", "FAILED": "FAILED",
                          "TIMEOUT": "TIMEOUT", "SKIPPED": "SKIPPED"}
            status = status_map.get(raw_status, raw_status)
            duration = float(m.group(4))
            cases.append(TestCase(name=name, status=status, duration_s=duration))
    return cases


def _parse_lcov_summary(text: str) -> tuple[float, float, float]:
    """Extract line/function/branch percentages from lcov --summary output."""
    import re
    def _extract(pattern: str) -> float:
        m = re.search(pattern, text)
        return float(m.group(1)) if m else 0.0

    line_pct = _extract(r"lines\.*:\s+([\d.]+)%")
    fn_pct   = _extract(r"functions\.*:\s+([\d.]+)%")
    br_pct   = _extract(r"branches\.*:\s+([\d.]+)%")
    return line_pct, fn_pct, br_pct
