"""
coverage_parser.py
------------------
Parses an lcov .info file and returns structured coverage data
that the agent can reason about.

An lcov .info file is like a detailed receipt for a test run —
every line in every source file gets a "hit count" stamp.
This module reads that receipt and distills it into:
  - per-file line / function / branch percentages
  - names of functions that were never called (coverage_pct == 0)
  - a human-readable summary string per file for the AI prompt

No external dependencies — parses the text format directly.
"""

from dataclasses import dataclass, field
from pathlib import Path


# ──────────────────────────────────────────────────────────────────
# Data types
# ──────────────────────────────────────────────────────────────────

@dataclass
class FunctionCoverage:
    name: str
    hit_count: int      # 0 = never called


@dataclass
class FileCoverage:
    src_file: str
    line_found: int = 0
    line_hit: int = 0
    function_found: int = 0
    function_hit: int = 0
    branch_found: int = 0
    branch_hit: int = 0
    functions: list[FunctionCoverage] = field(default_factory=list)

    @property
    def line_coverage_pct(self) -> float:
        return (self.line_hit / self.line_found * 100.0) if self.line_found else 100.0

    @property
    def function_coverage_pct(self) -> float:
        return (self.function_hit / self.function_found * 100.0) if self.function_found else 100.0

    @property
    def branch_coverage_pct(self) -> float:
        return (self.branch_hit / self.branch_found * 100.0) if self.branch_found else 100.0

    @property
    def uncovered_functions(self) -> list[str]:
        return [f.name for f in self.functions if f.hit_count == 0]

    @property
    def coverage_summary(self) -> str:
        """One-paragraph text summary for the AI prompt."""
        lines = [
            f"Coverage for {self.src_file}:",
            f"  Lines:     {self.line_hit}/{self.line_found} = {self.line_coverage_pct:.1f}%",
            f"  Functions: {self.function_hit}/{self.function_found} = {self.function_coverage_pct:.1f}%",
            f"  Branches:  {self.branch_hit}/{self.branch_found} = {self.branch_coverage_pct:.1f}%",
        ]
        uf = self.uncovered_functions
        if uf:
            lines.append(f"  Uncovered functions ({len(uf)}):")
            for fn in uf:
                lines.append(f"    - {fn}()")
        else:
            lines.append("  All functions covered ✅")
        return "\n".join(lines)

    @property
    def has_gaps(self) -> bool:
        return (self.line_coverage_pct < 100.0 or
                self.function_coverage_pct < 100.0 or
                self.branch_coverage_pct < 100.0)


@dataclass
class CoverageReport:
    files: list[FileCoverage] = field(default_factory=list)

    @property
    def total_line_pct(self) -> float:
        found = sum(f.line_found for f in self.files)
        hit   = sum(f.line_hit   for f in self.files)
        return (hit / found * 100.0) if found else 100.0

    @property
    def total_function_pct(self) -> float:
        found = sum(f.function_found for f in self.files)
        hit   = sum(f.function_hit   for f in self.files)
        return (hit / found * 100.0) if found else 100.0

    @property
    def total_branch_pct(self) -> float:
        found = sum(f.branch_found for f in self.files)
        hit   = sum(f.branch_hit   for f in self.files)
        return (hit / found * 100.0) if found else 100.0

    @property
    def files_with_gaps(self) -> list[FileCoverage]:
        return [f for f in self.files if f.has_gaps]


# ──────────────────────────────────────────────────────────────────
# Parser
# ──────────────────────────────────────────────────────────────────

def parse_lcov_info(info_path: str, project_root: str = ".") -> CoverageReport:
    """
    Parse an lcov .info file and return a CoverageReport.

    lcov .info format (one record per source file):
        SF:<source file path>
        FN:<line>,<function name>
        FNDA:<hit count>,<function name>
        FNF:<functions found>
        FNH:<functions hit>
        DA:<line>,<hit count>
        BRF:<branches found>
        BRH:<branches hit>
        LF:<lines found>
        LH:<lines hit>
        end_of_record
    """
    p = Path(info_path)
    if not p.exists():
        return CoverageReport()

    report = CoverageReport()
    current: FileCoverage | None = None
    fn_hits: dict[str, int] = {}   # name → hit count for current file

    for raw_line in p.read_text(errors="replace").splitlines():
        line = raw_line.strip()

        if line.startswith("SF:"):
            src = line[3:].strip()
            # Normalise to relative path for display
            try:
                src = str(Path(src).relative_to(Path(project_root).resolve()))
            except (ValueError, OSError):
                pass
            current = FileCoverage(src_file=src)
            fn_hits = {}

        elif line.startswith("FNDA:") and current is not None:
            # FNDA:<hit count>,<name>
            rest = line[5:]
            comma = rest.index(",")
            hit_count = int(rest[:comma])
            fn_name = rest[comma + 1:].strip()
            fn_hits[fn_name] = hit_count

        elif line.startswith("FNF:") and current is not None:
            current.function_found = int(line[4:])

        elif line.startswith("FNH:") and current is not None:
            current.function_hit = int(line[4:])

        elif line.startswith("LF:") and current is not None:
            current.line_found = int(line[3:])

        elif line.startswith("LH:") and current is not None:
            current.line_hit = int(line[3:])

        elif line.startswith("BRF:") and current is not None:
            current.branch_found = int(line[4:])

        elif line.startswith("BRH:") and current is not None:
            current.branch_hit = int(line[4:])

        elif line == "end_of_record" and current is not None:
            # Materialise FunctionCoverage objects
            current.functions = [
                FunctionCoverage(name=name, hit_count=hits)
                for name, hits in sorted(fn_hits.items())
            ]
            report.files.append(current)
            current = None
            fn_hits = {}

    return report


def get_gaps_for_src_file(report: CoverageReport, src_file: str) -> FileCoverage | None:
    """
    Look up coverage data for a single source file.
    Tries exact match first, then basename match (tolerates path prefix differences).
    """
    needle = src_file.replace("\\", "/")
    needle_base = Path(needle).name

    for fc in report.files:
        candidate = fc.src_file.replace("\\", "/")
        if candidate == needle or candidate.endswith("/" + needle):
            return fc
        if Path(candidate).name == needle_base:
            return fc

    return None
