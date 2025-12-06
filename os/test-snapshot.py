#!/usr/bin/env python3
"""
Snapshot test runner for the Otium kernel.

Runs test programs, captures TEST: output lines, and compares against saved snapshots.
Supports both RISC-V (via QEMU) and WASM (via Node.js) platforms.

Uses Meson for building.
"""

import subprocess
import sys
import os
import shutil
import hashlib
import gzip
from pathlib import Path
from typing import List, Tuple, Callable, Optional

# Test programs to run (map of test name to kernel prog constant)
# TODO: These should become Meson configuration options
TEST_PROGRAMS = {
    "test-hello": "KERNEL_PROG_TEST_HELLO",
    "test-mem": "KERNEL_PROG_TEST_MEM",
    "test-alternate": "KERNEL_PROG_TEST_ALTERNATE",
    "test-ipc": "KERNEL_PROG_TEST_IPC",
    "test-ipc-ordering": "KERNEL_PROG_TEST_IPC_ORDERING",
    "test-graphics": "KERNEL_PROG_TEST_GRAPHICS",
    "test-filesystem": "KERNEL_PROG_TEST_FILESYSTEM",
    "test-filesystem-fat": "KERNEL_PROG_TEST_FILESYSTEM_FAT",
}

# Platform-specific test exclusions
PLATFORM_EXCLUSIONS = {
    "wasm": ["test-mem", "test-filesystem-fat"],  # Skip memory tests and FAT tests on WASM
}

# Tests that require disk image handling
FILESYSTEM_DISK_TESTS = {"test-filesystem-fat"}

# Test-specific Meson options
TEST_MESON_OPTIONS = {
    "test-graphics": ["-Dgraphics_backend=test"],  # Use 16x16 test backend for snapshot testing
    "test-filesystem": ["-Dfilesystem_backend=memory"],  # Use in-memory filesystem backend
    "test-filesystem-fat": ["-Dfilesystem_backend=fat"],  # Use FAT filesystem backend
}

SNAPSHOT_DIR = Path(__file__).parent / "snapshots"
PROJECT_ROOT = Path(__file__).parent

# Disk image paths
TEST_IMAGES_DIR = PROJECT_ROOT / "test-images"
BASELINE_DISK_IMAGE_GZ = TEST_IMAGES_DIR / "fat-baseline.img.gz"
WORKING_DISK_IMAGE = PROJECT_ROOT / "disk.fat.img"

# Platform-specific configuration
PLATFORMS = {
    "riscv": {
        "build_dir": PROJECT_ROOT / "build-riscv-test",
        "cross_file": "cross/riscv32.txt",
        "binary": "os.elf",
        "timeout": 10,
        "runner": PROJECT_ROOT / "tools" / "run-qemu-riscv.sh",
    },
    "wasm": {
        "build_dir": PROJECT_ROOT / "build-wasm-test",
        "cross_file": "cross/wasm.txt",
        "binary": "os.js",
        "timeout": 5,
        "runner": PROJECT_ROOT / "tools" / "run-wasm.sh",
    }
}


def compute_sha256(file_path: Path) -> str:
    """Compute SHA256 hash of a file."""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            sha256_hash.update(chunk)
    return sha256_hash.hexdigest()


def setup_disk_image(test_name: str) -> None:
    """Decompress baseline disk image for tests that need it."""
    if test_name in FILESYSTEM_DISK_TESTS:
        if not BASELINE_DISK_IMAGE_GZ.exists():
            raise FileNotFoundError(f"Baseline disk image not found: {BASELINE_DISK_IMAGE_GZ}")
        print(f"  Decompressing {BASELINE_DISK_IMAGE_GZ.name} to {WORKING_DISK_IMAGE}")
        with gzip.open(BASELINE_DISK_IMAGE_GZ, 'rb') as f_in:
            with open(WORKING_DISK_IMAGE, 'wb') as f_out:
                shutil.copyfileobj(f_in, f_out)


def get_disk_image_sha256(test_name: str) -> Optional[str]:
    """Get SHA256 of the working disk image after test completes."""
    if test_name in FILESYSTEM_DISK_TESTS and WORKING_DISK_IMAGE.exists():
        return compute_sha256(WORKING_DISK_IMAGE)
    return None


def run_platform(platform_config: dict, disk_image: Optional[Path] = None) -> str:
    """Run the platform using the appropriate runner script."""
    timeout = platform_config["timeout"]
    runner = platform_config["runner"]
    build_dir = platform_config["build_dir"]

    # Set test mode environment variable for WASM
    env = os.environ.copy()
    env['OTIUM_TEST_MODE'] = '1'

    run_cmd = [str(runner), str(build_dir)]
    if disk_image:
        run_cmd.append(str(disk_image))
    print(f"  $ {' '.join(run_cmd)}")

    try:
        result = subprocess.run(
            run_cmd,
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


def run_test(test_name: str, kernel_prog: str, platform: str) -> Tuple[List[str], str]:
    """
    Compile and run a test program on specified platform.

    Args:
        test_name: Name of the test
        kernel_prog: Kernel program constant (e.g., KERNEL_PROG_TEST_HELLO)
        platform: "riscv" or "wasm"

    Returns:
        Tuple of (List of TEST: output lines, Full output string)
    """
    print(f"Running test: {test_name} on {platform}")

    platform_config = PLATFORMS[platform]
    build_dir = platform_config["build_dir"]
    cross_file = platform_config["cross_file"]

    # Convert kernel_prog constant to meson option value
    # KERNEL_PROG_TEST_HELLO -> test_hello
    kernel_prog_option = kernel_prog.replace('KERNEL_PROG_', '').lower()

    print(f"  Configuring with kernel_prog={kernel_prog_option}")

    # Always reconfigure to ensure correct test mode
    # Use --reconfigure to update options without full rebuild
    print(f"  Setting up Meson build in {build_dir}")
    try:
        setup_cmd = [
            "meson", "setup", str(build_dir),
            f"--cross-file={cross_file}",
            f"-Dkernel_prog={kernel_prog_option}",
            "--reconfigure"
        ]

        # Add test-specific Meson options if defined
        if test_name in TEST_MESON_OPTIONS:
            setup_cmd.extend(TEST_MESON_OPTIONS[test_name])

        print(f"  $ {' '.join(setup_cmd)}")
        subprocess.run(
            setup_cmd,
            check=True,
            capture_output=True,
            text=True,
            cwd=str(PROJECT_ROOT)
        )
    except subprocess.CalledProcessError as e:
        print(f"  ERROR: Meson setup failed")
        print(f"  stdout: {e.stdout}")
        print(f"  stderr: {e.stderr}")
        raise

    # Compile with Meson
    print(f"  Compiling with Meson...")
    try:
        compile_cmd = ["meson", "compile", "-C", str(build_dir)]
        print(f"  $ {' '.join(compile_cmd)}")
        subprocess.run(
            compile_cmd,
            check=True,
            capture_output=True,
            text=True,
            cwd=str(PROJECT_ROOT)
        )
    except subprocess.CalledProcessError as e:
        print(f"  ERROR: Compilation failed")
        print(f"  stdout: {e.stdout}")
        print(f"  stderr: {e.stderr}")
        raise

    # Set up disk image if needed (before running)
    setup_disk_image(test_name)

    # Run with platform runner script
    # For filesystem tests, pass the disk image path to skip create/extract
    disk_image = WORKING_DISK_IMAGE if test_name in FILESYSTEM_DISK_TESTS else None
    print(f"  Running on {platform} (timeout: {platform_config['timeout']}s)")
    output = run_platform(platform_config, disk_image)

    # Extract TEST: lines
    test_lines = extract_test_lines(output)

    # Add disk image sha256 if this test uses disk images
    disk_sha256 = get_disk_image_sha256(test_name)
    if disk_sha256:
        test_lines.append(f"TEST: IMAGE_SHA256={disk_sha256}")
        print(f"  Disk image SHA256: {disk_sha256}")

    print(f"  Captured {len(test_lines)} TEST: lines")
    return test_lines, output


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


def compare_snapshots(test_name: str, actual: List[str], expected: List[str], full_output: str = "") -> bool:
    """
    Compare actual output against expected snapshot.

    Args:
        test_name: Name of the test
        actual: Actual TEST: lines captured
        expected: Expected TEST: lines from snapshot
        full_output: Full output from the test run (shown on mismatch)

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

        # Show full output to help debug
        if full_output:
            print("\n  Full output from test:")
            print("  " + "-" * 60)
            for line in full_output.split('\n'):
                print(f"  {line}")
            print("  " + "-" * 60)

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
    parser.add_argument(
        "--test-pattern",
        type=str,
        default=None,
        help="Only run tests whose name contains this pattern (case-insensitive)"
    )
    args = parser.parse_args()

    # Determine which platforms to test
    platforms_to_test = (
        ["riscv", "wasm"] if args.platform == "all"
        else [args.platform]
    )

    # Filter tests by pattern if provided
    tests_to_run = TEST_PROGRAMS
    if args.test_pattern:
        pattern = args.test_pattern.lower()
        tests_to_run = {
            name: prog for name, prog in TEST_PROGRAMS.items()
            if pattern in name.lower()
        }
        if not tests_to_run:
            print(f"No tests match pattern '{args.test_pattern}'")
            sys.exit(1)
        print(f"Running {len(tests_to_run)} test(s) matching pattern '{args.test_pattern}':")
        for name in tests_to_run:
            print(f"  - {name}")
        print()

    results = {}

    for platform in platforms_to_test:
        for test_name, kernel_prog in tests_to_run.items():
            # Skip tests excluded for this platform
            if test_name in PLATFORM_EXCLUSIONS.get(platform, []):
                print(f"Skipping {test_name} on {platform} (excluded)")
                continue

            test_key = f"{test_name}-{platform}"

            try:
                # Run the test
                actual_output, full_output = run_test(test_name, kernel_prog, platform)

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
                        passed = compare_snapshots(test_name, actual_output, expected_output, full_output)
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
