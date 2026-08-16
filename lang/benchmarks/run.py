#!/usr/bin/env python3

"""Run standalone Otium programs and summarize their wall-clock times."""

import argparse
import statistics
import subprocess
import sys
import time
from pathlib import Path


def nonnegative_int(value):
    parsed = int(value)
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def positive_int(value):
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def parse_args():
    parser = argparse.ArgumentParser(
        description="benchmark standalone Otium programs"
    )
    parser.add_argument("binary", type=Path, help="path to the otium executable")
    parser.add_argument(
        "benchmarks",
        nargs="*",
        type=Path,
        help=".scm files to run (default: every benchmark)",
    )
    parser.add_argument(
        "--warmups",
        type=nonnegative_int,
        default=1,
        help="warmup runs per benchmark (default: 1)",
    )
    parser.add_argument(
        "--runs",
        type=positive_int,
        default=5,
        help="measured runs per benchmark (default: 5)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=120.0,
        help="timeout for each run in seconds (default: 120)",
    )
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def run_once(binary, benchmark, timeout):
    started = time.perf_counter()
    try:
        result = subprocess.run(
            [binary, benchmark],
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"timed out after {timeout:g}s") from error
    elapsed = time.perf_counter() - started

    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip() or "no output"
        raise RuntimeError(f"exited with status {result.returncode}: {details}")
    return elapsed


def format_duration(seconds):
    if seconds < 1:
        return f"{seconds * 1000:.2f} ms"
    return f"{seconds:.3f} s"


def main():
    args = parse_args()
    binary = args.binary.resolve()
    benchmark_dir = Path(__file__).resolve().parent
    requested = args.benchmarks or sorted(benchmark_dir.glob("*.scm"))
    benchmarks = [benchmark.resolve() for benchmark in requested]

    if not binary.is_file():
        print(f"error: executable not found: {binary}", file=sys.stderr)
        return 2
    if not benchmarks:
        print(f"error: no benchmarks found in {benchmark_dir}", file=sys.stderr)
        return 2
    missing = [benchmark for benchmark in benchmarks if not benchmark.is_file()]
    if missing:
        print(f"error: benchmark not found: {missing[0]}", file=sys.stderr)
        return 2

    name_width = max(len("benchmark"), *(len(benchmark.stem) for benchmark in benchmarks))
    print(
        f"{'benchmark':<{name_width}}  {'median':>10}  {'min':>10}  "
        f"{'max':>10}  {'runs':>4}"
    )

    for benchmark in benchmarks:
        try:
            for _ in range(args.warmups):
                run_once(binary, benchmark, args.timeout)
            samples = [
                run_once(binary, benchmark, args.timeout) for _ in range(args.runs)
            ]
        except RuntimeError as error:
            print(f"error: {benchmark.name}: {error}", file=sys.stderr)
            return 1

        print(
            f"{benchmark.stem:<{name_width}}  "
            f"{format_duration(statistics.median(samples)):>10}  "
            f"{format_duration(min(samples)):>10}  "
            f"{format_duration(max(samples)):>10}  "
            f"{len(samples):>4}"
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
