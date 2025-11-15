#!/usr/bin/env python3
"""
Snapshot test runner for the Otium kernel.

Runs test programs, captures TEST: output lines, and compares against saved snapshots.
Supports both RISC-V (via QEMU) and WASM (via Node.js) platforms.
"""

import subprocess
import sys
import os
from pathlib import Path
from typing import List, Tuple, Callable

# Test programs to run (map of test name to config.sh flag)
TEST_PROGRAMS = {
    "test-hello": "--test-hello",
    "test-mem": "--test-mem",
    "test-alternate": "--test-alternate",
    "test-ipc": "--test-ipc",
}

# Platform-specific test exclusions
PLATFORM_EXCLUSIONS = {
    "wasm": ["test-mem", "test-ipc"],  # Skip memory and IPC tests on WASM for now
}

SNAPSHOT_DIR = Path(__file__).parent / "snapshots"
CONFIG_SCRIPT = Path(__file__).parent / "config.sh"

# Platform-specific configuration
PLATFORMS = {
    "riscv": {
        "compile_script": Path(__file__).parent / "compile-riscv.sh",
        "timeout": 10,
        "qemu_cmd": "qemu-system-riscv32",
    },
    "wasm": {
        "compile_script": Path(__file__).parent / "compile-wasm.sh",
        "timeout": 5,
        "node_script": Path(__file__).parent / "run-wasm.js",
    }
}


def run_qemu(platform_config: dict) -> str:
    """Run QEMU and capture output."""
    timeout = platform_config["timeout"]
    qemu_cmd = platform_config["qemu_cmd"]

    try:
        result = subprocess.run(
            [
                qemu_cmd,
                "-machine", "virt",
                "-bios", "default",
                "-nographic",
                "-serial", "mon:stdio",
                "--no-reboot",
                "-kernel", "bin/os.elf"
            ],
            capture_output=True,
            text=True,
            timeout=timeout
        )
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        # Timeout is expected for some tests
        return (e.stdout or "") + (e.stderr or "")


def run_node_wasm(platform_config: dict) -> str:
    """Run Node.js with WASM kernel and capture output."""
    timeout = platform_config["timeout"]
    node_script = platform_config["node_script"]

    # Set test mode environment variable
    env = os.environ.copy()
    env['OTIUM_TEST_MODE'] = '1'

    try:
        result = subprocess.run(
            ["node", str(node_script)],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env
        )
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired as e:
        # Timeout is expected for some tests
        return (e.stdout or "") + (e.stderr or "")


def extract_test_lines(output: str) -> List[str]:
    """Extract TEST: lines from output."""
    return [
        line.strip()
        for line in output.split('\n')
        if line.strip().startswith('TEST:')
    ]


def run_test(test_name: str, config_flag: str, platform: str) -> List[str]:
    """
    Compile and run a test program on specified platform.

    Args:
        test_name: Name of the test
        config_flag: Flag to pass to config.sh
        platform: "riscv" or "wasm"

    Returns:
        List of TEST: output lines
    """
    print(f"Running test: {test_name} on {platform}")

    platform_config = PLATFORMS[platform]

    # Generate configuration
    print(f"  Configuring with flag: {config_flag}")
    try:
        subprocess.run(
            [str(CONFIG_SCRIPT), config_flag],
            check=True,
            capture_output=True,
            text=True
        )
    except subprocess.CalledProcessError as e:
        print(f"  ERROR: Configuration failed")
        print(f"  stdout: {e.stdout}")
        print(f"  stderr: {e.stderr}")
        raise

    # Compile the kernel with platform-specific script
    print(f"  Compiling for {platform}...")
    try:
        subprocess.run(
            [str(platform_config["compile_script"])],
            check=True,
            capture_output=True,
            text=True
        )
    except subprocess.CalledProcessError as e:
        print(f"  ERROR: Compilation failed")
        print(f"  stdout: {e.stdout}")
        print(f"  stderr: {e.stderr}")
        raise

    # Run with platform-specific runner
    print(f"  Running on {platform} (timeout: {platform_config['timeout']}s)")
    if platform == "riscv":
        output = run_qemu(platform_config)
    elif platform == "wasm":
        output = run_node_wasm(platform_config)
    else:
        raise ValueError(f"Unknown platform: {platform}")

    # Extract TEST: lines
    test_lines = extract_test_lines(output)

    print(f"  Captured {len(test_lines)} TEST: lines")
    return test_lines


def get_snapshot_path(test_name: str, platform: str) -> Path:
    """Get snapshot file path for test and platform."""
    return SNAPSHOT_DIR / f"{test_name}-{platform}.txt"


def save_snapshot(test_name: str, platform: str, lines: List[str]) -> None:
    """Save snapshot for a test on a specific platform."""
    SNAPSHOT_DIR.mkdir(exist_ok=True)
    snapshot_file = get_snapshot_path(test_name, platform)

    with open(snapshot_file, 'w') as f:
        for line in lines:
            f.write(line + '\n')

    print(f"  Saved snapshot to {snapshot_file}")


def load_snapshot(test_name: str, platform: str) -> List[str]:
    """Load snapshot for a test on a specific platform."""
    snapshot_file = get_snapshot_path(test_name, platform)

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
    parser.add_argument(
        "--platform",
        choices=["riscv", "wasm", "all"],
        default="all",
        help="Platform to test (riscv, wasm, or all)"
    )
    args = parser.parse_args()

    # Determine which platforms to test
    platforms_to_test = (
        ["riscv", "wasm"] if args.platform == "all"
        else [args.platform]
    )

    results = {}

    for platform in platforms_to_test:
        for test_name, config_flag in TEST_PROGRAMS.items():
            # Skip tests excluded for this platform
            if test_name in PLATFORM_EXCLUSIONS.get(platform, []):
                print(f"Skipping {test_name} on {platform} (excluded)")
                continue

            test_key = f"{test_name}-{platform}"

            try:
                # Run the test
                actual_output = run_test(test_name, config_flag, platform)

                if args.update:
                    # Update mode: save the snapshot
                    save_snapshot(test_name, platform, actual_output)
                    results[test_key] = "UPDATED"
                else:
                    # Test mode: compare against snapshot
                    expected_output = load_snapshot(test_name, platform)

                    if not expected_output:
                        print(f"  ⚠ WARNING: No snapshot found for {test_key}")
                        print(f"  Run with --update to create snapshot")
                        results[test_key] = "NO_SNAPSHOT"
                    else:
                        passed = compare_snapshots(test_name, actual_output, expected_output)
                        results[test_key] = "PASS" if passed else "FAIL"

            except Exception as e:
                print(f"  ✗ ERROR: {e}")
                results[test_key] = "ERROR"

            print()

    # Print summary
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    for test_key, result in results.items():
        symbol = {
            "PASS": "✓",
            "FAIL": "✗",
            "ERROR": "✗",
            "NO_SNAPSHOT": "⚠",
            "UPDATED": "↻"
        }.get(result, "?")
        print(f"{symbol} {test_key}: {result}")

    # Exit with error code if any tests failed
    if any(r in ["FAIL", "ERROR"] for r in results.values()):
        sys.exit(1)

    if all(r == "NO_SNAPSHOT" for r in results.values()):
        print("\nNo snapshots found. Run with --update to create them.")
        sys.exit(1)


if __name__ == "__main__":
    main()
