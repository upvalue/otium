#!/usr/bin/env python3
"""Check simple textual invariants for Otium source files."""

import re
import sys
from pathlib import Path


RULES = {
    "weak attribute": re.compile(r"__attribute__\s*\(\([^\n]*\bweak\b|\[\[\s*gnu::weak\s*\]\]"),
    "alias attribute": re.compile(r"__attribute__\s*\(\([^\n]*\balias\s*\(|\[\[\s*gnu::alias\b"),
}


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    failures = []
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}:
            continue
        for line_no, line in enumerate(path.read_text().splitlines(), 1):
            for name, pattern in RULES.items():
                if pattern.search(line):
                    failures.append(f"{path.relative_to(root)}:{line_no}: banned {name}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
