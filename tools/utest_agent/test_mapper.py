"""
test_mapper.py
--------------
Maps a cliforge src/ file to its corresponding unit-test folder and
reads back the existing test files.

The mapping convention for cliforge:
    src/cf_lex.c    →  tests/unit-tests/ut_cf_lex/
    src/cf_parse.c  →  tests/unit-tests/ut_cf_parse/
    src/cf_gen.c    →  tests/unit-tests/ut_cf_gen/
    src/cf_util.c   →  tests/unit-tests/ut_cf_util/
    src/main.c      →  (no dedicated unit-test folder)

Think of this like a filing cabinet:
    src file = the document you changed
    test folder = the drawer where its test paperwork lives

If the drawer doesn't exist yet, that's exactly what the agent
will flag: "you touched this file but there is no test folder."
"""

import re
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class TestFile:
    """One existing test file inside a test folder."""
    path: str
    test_names: list[str] = field(default_factory=list)  # TEST(suite, name) found inside


@dataclass
class TestFolder:
    """Everything the agent knows about the test folder for one src file."""
    src_file: str
    folder_path: str
    exists: bool
    existing_tests: list[TestFile] = field(default_factory=list)
    all_test_names: list[str] = field(default_factory=list)


# Matches:  TEST(SuiteName, TestName)  or  TEST_F(Fixture, Name)
_GTEST_RE = re.compile(r"\bTEST(?:_F|_P)?\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")

# Matches C function definitions:  return_type  func_name(...)  {
_CFUNC_DEF_RE = re.compile(
    r"^(?:static\s+)?(?:inline\s+)?[a-zA-Z_][\w\s\*]+?\b([a-zA-Z_]\w*)\s*\([^;]*\)\s*\{?\s*$",
    re.MULTILINE
)
_CFUNC_SKIP = {"if", "for", "while", "switch", "else", "do", "return",
               "TEST", "TEST_F", "TEST_P", "EXPECT_EQ", "ASSERT_EQ"}


def src_to_test_folder(src_file: str, tests_root: str = "tests/unit-tests") -> str:
    """
    Convert a src file path to the expected test folder path.

    Examples (with tests_root = "tests/unit-tests"):
        src/cf_lex.c    → tests/unit-tests/ut_cf_lex
        src/cf_parse.c  → tests/unit-tests/ut_cf_parse
        src/main.c      → tests/unit-tests/ut_main
    """
    stem = Path(src_file).stem          # "cf_lex"
    folder_name = f"ut_{stem}"          # "ut_cf_lex"
    return str(Path(tests_root) / folder_name)


def scan_test_folder(folder_path: str, src_file: str) -> TestFolder:
    """
    Read a test folder and return a TestFolder describing its contents.
    If the folder does not exist, returns a TestFolder with exists=False.
    """
    p = Path(folder_path)
    if not p.exists() or not p.is_dir():
        return TestFolder(
            src_file=src_file,
            folder_path=folder_path,
            exists=False,
        )

    test_files: list[TestFile] = []
    all_names: list[str] = []

    for cpp_file in sorted(p.rglob("*.cpp")):
        try:
            text = cpp_file.read_text(errors="replace")
        except OSError:
            continue

        names = [
            f"{m.group(1)}.{m.group(2)}"
            for m in _GTEST_RE.finditer(text)
        ]
        tf = TestFile(path=str(cpp_file), test_names=names)
        test_files.append(tf)
        all_names.extend(names)

    return TestFolder(
        src_file=src_file,
        folder_path=folder_path,
        exists=True,
        existing_tests=test_files,
        all_test_names=all_names,
    )


def read_src_file_functions(src_file: str) -> list[str]:
    """
    Extract all function names defined in a C source file.
    Used for full-scan mode: we want to check coverage for every function,
    not just the ones that appeared in the diff.
    """
    p = Path(src_file)
    if not p.exists():
        return []

    try:
        text = p.read_text(errors="replace")
    except OSError:
        return []

    names = []
    seen = set()
    for m in _CFUNC_DEF_RE.finditer(text):
        name = m.group(1)
        if name not in _CFUNC_SKIP and name not in seen:
            names.append(name)
            seen.add(name)
    return names
