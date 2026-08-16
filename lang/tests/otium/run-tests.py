#!/usr/bin/env python3
"""Portable Otium conformance suite runner.

Usage: run-tests.py [options] /path/to/otium-binary [test-name ...]

For each NN-name.scm in this directory (or only the named tests), runs
`binary NN-name.scm` and diffs its stdout against expected/NN-name.txt.
Tests run in parallel (they are independent processes); output is printed
in test order as results arrive. Exit code is the number of failing tests.

Options:
  --filter SUBSTR   only run tests whose name contains SUBSTR
  --skip SUBSTR     skip tests whose name contains SUBSTR (repeatable)
  --timeout SEC     per-test timeout in seconds (default 60)
  --jobs N          max concurrent tests (default: cpu count)
"""

import argparse
import difflib
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


def run_one(binary, here, expected_dir, test, timeout):
    """Returns (name, passed, lines-to-print)."""
    name = test[: -len(".scm")]
    expected_path = os.path.join(expected_dir, name + ".txt")
    try:
        with open(expected_path, encoding="utf-8") as f:
            expected = f.read()
    except FileNotFoundError:
        return name, False, [f"FAIL {name}: missing {expected_path}"]

    try:
        proc = subprocess.run(
            [binary, os.path.join(here, test)],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except OSError as e:
        return name, False, [f"FAIL {name}: could not run binary: {e}"]
    except subprocess.TimeoutExpired:
        return name, False, [f"FAIL {name}: timed out after {timeout}s"]

    actual = proc.stdout
    if actual == expected and proc.returncode == 0:
        return name, True, [f"PASS {name}"]

    lines = [f"FAIL {name}"]
    if proc.returncode != 0:
        lines.append(f"  exit code {proc.returncode}")
    if proc.stderr.strip():
        for line in proc.stderr.rstrip().splitlines():
            lines.append(f"  stderr: {line}")
    if actual != expected:
        diff = difflib.unified_diff(
            expected.splitlines(keepends=True),
            actual.splitlines(keepends=True),
            fromfile=f"expected/{name}.txt",
            tofile=f"{name} stdout",
        )
        for line in diff:
            lines.append("  " + line.rstrip("\n"))
    return name, False, lines


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--filter", default=None)
    ap.add_argument("--skip", action="append", default=[])
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("binary", nargs="?")
    ap.add_argument("names", nargs="*")
    args = ap.parse_args()

    if not args.binary:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    here = os.path.dirname(os.path.abspath(__file__))
    expected_dir = os.path.join(here, "expected")

    tests = sorted(
        f for f in os.listdir(here)
        if f.endswith(".scm") and f[:2].isdigit()
    )
    if args.names:
        only = set(args.names)
        tests = [t for t in tests
                 if t in only or t[: -len(".scm")] in only]
    if args.filter:
        tests = [t for t in tests if args.filter in t]
    for pat in args.skip:
        tests = [t for t in tests if pat not in t]
    if not tests:
        print("no tests found", file=sys.stderr)
        return 2

    failures = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futs = [
            pool.submit(run_one, args.binary, here, expected_dir, t, args.timeout)
            for t in tests
        ]
        for fut in futs:  # print in test order as results complete
            _, passed, lines = fut.result()
            if not passed:
                failures += 1
            for line in lines:
                print(line)

    total = len(tests)
    print(f"\n{total - failures}/{total} passed, {failures} failed")
    return failures


if __name__ == "__main__":
    sys.exit(main())
