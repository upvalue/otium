#!/usr/bin/env python3
"""
Snapshot test runner for the Otium kernel.

Runs test programs, captures TEST: output lines, and compares against saved snapshots.
"""

import subprocess
import sys
import os
from pathlib import Path
from typing import List, Tuple

# Test programs to run (map of test name to compile flag)
TEST_PROGRAMS = {
    "test-hello": "--test-hello",
}

QEMU_TIMEOUT = 10  # seconds
SNAPSHOT_DIR = Path(__file__).parent / "snapshots"
COMPILE_SCRIPT = Path(__file__).parent / "compile-riscv.sh"
QEMU_CMD = "qemu-system-riscv32"


def run_test(test_name: str, compile_flag: str) -> List[str]:
    """
    Compile and run a test program, returning TEST: output lines.

    Args:
        test_name: Name of the test
        compile_flag: Flag to pass to compile-riscv.sh

    Returns:
        List of TEST: output lines
    """
    print(f"Running test: {test_name}")

    # Compile the kernel with the test flag
    print(f"  Compiling with flag: {compile_flag}")
    try:
        subprocess.run(
            [str(COMPILE_SCRIPT), compile_flag],
            check=True,
            capture_output=True,
            text=True
        )
    except subprocess.CalledProcessError as e:
        print(f"  ERROR: Compilation failed")
        print(f"  stdout: {e.stdout}")
        print(f"  stderr: {e.stderr}")
        raise

    # Run QEMU and capture output
    print(f"  Running QEMU (timeout: {QEMU_TIMEOUT}s)")
    try:
        result = subprocess.run(
            [
                QEMU_CMD,
                "-machine", "virt",
                "-bios", "default",
                "-nographic",
                "-serial", "mon:stdio",
                "--no-reboot",
                "-kernel", "otk/kernel.elf"
            ],
            capture_output=True,
            text=True,
            timeout=QEMU_TIMEOUT
        )
        output = result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        # Timeout is expected for some tests
        output = (e.stdout or "") + (e.stderr or "")

    # Extract TEST: lines
    test_lines = [
        line.strip()
        for line in output.split('\n')
        if line.strip().startswith('TEST:')
    ]

    print(f"  Captured {len(test_lines)} TEST: lines")
    return test_lines


def save_snapshot(test_name: str, lines: List[str]) -> None:
    """Save snapshot for a test."""
    SNAPSHOT_DIR.mkdir(exist_ok=True)
    snapshot_file = SNAPSHOT_DIR / f"{test_name}.txt"

    with open(snapshot_file, 'w') as f:
        for line in lines:
            f.write(line + '\n')

    print(f"  Saved snapshot to {snapshot_file}")


def load_snapshot(test_name: str) -> List[str]:
    """Load snapshot for a test."""
    snapshot_file = SNAPSHOT_DIR / f"{test_name}.txt"

    if not snapshot_file.exists():
        return []

    with open(snapshot_file, 'r') as f:
        return [line.strip() for line in f if line.strip()]


def compare_snapshots(test_name: str, actual: List[str], expected: List[str]) -> bool:
    """
    Compare actual output against expected snapshot.

    Returns:
        True if snapshots match, False otherwise
    """
    if actual == expected:
        print(f"  ✓ PASS: Output matches snapshot")
        return True
    else:
        print(f"  ✗ FAIL: Output does not match snapshot")
        print(f"  Expected {len(expected)} lines, got {len(actual)} lines")

        # Show diff
        print("  Expected:")
        for line in expected:
            print(f"    {line}")
        print("  Actual:")
        for line in actual:
            print(f"    {line}")

        return False


def main():
    """Main test runner."""
    import argparse

    parser = argparse.ArgumentParser(description="Run snapshot tests for Otium kernel")
    parser.add_argument(
        "--update",
        action="store_true",
        help="Update snapshots instead of comparing"
    )
    args = parser.parse_args()

    results = {}

    for test_name, compile_flag in TEST_PROGRAMS.items():
        try:
            # Run the test
            actual_output = run_test(test_name, compile_flag)

            if args.update:
                # Update mode: save the snapshot
                save_snapshot(test_name, actual_output)
                results[test_name] = "UPDATED"
            else:
                # Test mode: compare against snapshot
                expected_output = load_snapshot(test_name)

                if not expected_output:
                    print(f"  ⚠ WARNING: No snapshot found for {test_name}")
                    print(f"  Run with --update to create snapshot")
                    results[test_name] = "NO_SNAPSHOT"
                else:
                    passed = compare_snapshots(test_name, actual_output, expected_output)
                    results[test_name] = "PASS" if passed else "FAIL"

        except Exception as e:
            print(f"  ✗ ERROR: {e}")
            results[test_name] = "ERROR"

        print()

    # Print summary
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    for test_name, result in results.items():
        symbol = {
            "PASS": "✓",
            "FAIL": "✗",
            "ERROR": "✗",
            "NO_SNAPSHOT": "⚠",
            "UPDATED": "↻"
        }.get(result, "?")
        print(f"{symbol} {test_name}: {result}")

    # Exit with error code if any tests failed
    if any(r in ["FAIL", "ERROR"] for r in results.values()):
        sys.exit(1)

    if all(r == "NO_SNAPSHOT" for r in results.values()):
        print("\nNo snapshots found. Run with --update to create them.")
        sys.exit(1)


if __name__ == "__main__":
    main()
