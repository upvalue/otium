#!/usr/bin/env python3
"""Check simple textual invariants for Otium source files."""

import re
import sys
from pathlib import Path


RULES = {
    "weak attribute": re.compile(r"__attribute__\s*\(\([^\n]*\bweak\b|\[\[\s*gnu::weak\s*\]\]"),
    "alias attribute": re.compile(r"__attribute__\s*\(\([^\n]*\balias\s*\(|\[\[\s*gnu::alias\b"),
    # All C-heap storage goes through the ot_alloc seam so an embedded host can
    # install its own allocator. vec.c implements the default backend and is the
    # only file allowed to name the libc functions.
    # The lookbehind keeps ot_alloc/ot_free and prose like "allocation-free (".
    "raw allocator call": re.compile(r"(?<![\w-])(?:malloc|calloc|realloc|free)\s*\("),
}

HEAP_RULES = {
    "heap-internals permit": re.compile(r"^\s*#\s*define\s+OT_HEAP_INTERNALS\b"),
    "heap header include": re.compile(r"^\s*#\s*include\s*[\"<][^\">]*heap\.h[\">]"),
    "heap layout access": re.compile(
        r"\b(?:heap_alloc|obj_payload|as_[A-Za-z0-9_]+|array_items|table_entries|"
        r"slots_items|entries_items|bytes_items|string_data_bytes|string_bytes|"
        r"buffer_data|code_consts|code_bytes|function_upvals|PairData|StringData|"
        r"ArrayData|TableData|TableEntry|BufferData|CodeData|FunctionData|ParamData|"
        r"RestartData|ForeignData|SlotsData|EntriesData|BytesData)\b"
    ),
}

# Rules that do not apply to specific files, keyed by rule name.
EXEMPT = {
    "raw allocator call": {"src/vec.c"},
}

HEAP_INTERNAL_FILES = {
    "src/heap.c",
    "src/vm.c",
    "src/slots.c",
    "src/collections.c",
}
HEAP_TEST_FILES = {
    "tests/test_substrate.c",
    "tests/test_builtins.c",
    "tests/test_compile.c",
    "tests/test_eval.c",
    "tests/test_vm.c",
}
HEAP_EXEMPT = {
    "heap-internals permit": HEAP_INTERNAL_FILES | HEAP_TEST_FILES | {"src/state.h"},
    "heap header include": HEAP_INTERNAL_FILES | HEAP_TEST_FILES | {"src/state.h"},
    "heap layout access": HEAP_INTERNAL_FILES | HEAP_TEST_FILES | {"src/heap.h"},
}


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    failures = []
    paths = []
    for source_dir in ("src", "repl", "ext", "tests"):
        paths.extend((root / source_dir).rglob("*"))
    for path in sorted(paths):
        if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}:
            continue
        relative = path.relative_to(root).as_posix()
        lines = path.read_text().splitlines()
        if relative.startswith("src/"):
            for line_no, line in enumerate(lines, 1):
                for name, pattern in RULES.items():
                    if relative in EXEMPT.get(name, ()):
                        continue
                    if pattern.search(line):
                        failures.append(f"{relative}:{line_no}: banned {name}")
        for line_no, line in enumerate(lines, 1):
            for name, pattern in HEAP_RULES.items():
                if relative in HEAP_EXEMPT.get(name, ()):
                    continue
                if pattern.search(line):
                    failures.append(f"{relative}:{line_no}: banned {name}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
