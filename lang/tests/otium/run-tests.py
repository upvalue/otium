#!/usr/bin/env python3
"""Portable Otium conformance suite runner.

Usage: run-tests.py /path/to/otium-binary [test-name ...]

For each NN-name.scm in this directory (or only the named tests), runs
`binary NN-name.scm` and diffs its stdout against expected/NN-name.txt.
Exit code is the number of failing tests.
"""

import difflib
import os
import subprocess
import sys


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    binary = sys.argv[1]
    only = set(sys.argv[2:])
    here = os.path.dirname(os.path.abspath(__file__))
    expected_dir = os.path.join(here, "expected")

    tests = sorted(
        f for f in os.listdir(here)
        if f.endswith(".scm") and f[:2].isdigit()
    )
    if only:
        tests = [t for t in tests
                 if t in only or t[:-len(".scm")] in only]
    if not tests:
        print("no tests found", file=sys.stderr)
        return 2

    failures = 0
    for test in tests:
        name = test[:-len(".scm")]
        expected_path = os.path.join(expected_dir, name + ".txt")
        try:
            with open(expected_path, encoding="utf-8") as f:
                expected = f.read()
        except FileNotFoundError:
            print(f"FAIL {name}: missing {expected_path}")
            failures += 1
            continue

        try:
            proc = subprocess.run(
                [binary, os.path.join(here, test)],
                capture_output=True,
                text=True,
                timeout=60,
            )
        except OSError as e:
            print(f"FAIL {name}: could not run binary: {e}")
            failures += 1
            continue
        except subprocess.TimeoutExpired:
            print(f"FAIL {name}: timed out after 60s")
            failures += 1
            continue

        actual = proc.stdout
        if actual == expected and proc.returncode == 0:
            print(f"PASS {name}")
            continue

        failures += 1
        print(f"FAIL {name}")
        if proc.returncode != 0:
            print(f"  exit code {proc.returncode}")
        if proc.stderr.strip():
            for line in proc.stderr.rstrip().splitlines():
                print(f"  stderr: {line}")
        if actual != expected:
            diff = difflib.unified_diff(
                expected.splitlines(keepends=True),
                actual.splitlines(keepends=True),
                fromfile=f"expected/{name}.txt",
                tofile=f"{name} stdout",
            )
            for line in diff:
                print("  " + line.rstrip("\n"))

    total = len(tests)
    print(f"\n{total - failures}/{total} passed, {failures} failed")
    return failures


if __name__ == "__main__":
    sys.exit(main())
