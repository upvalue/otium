#!/usr/bin/env python3

"""Build both collectors under one reservation-plus-metadata budget."""

import argparse
import csv
import json
import statistics
import struct
import subprocess
import sys
import time
from pathlib import Path


MIB = 1024 * 1024
NURSERY_BYTES = 2 * MIB
MARK_STACK_ENTRIES = 16384
WORKLOADS = {
    "churn": 1_000_000,
    "mixed": 30_000,
    "fragmentation": 8_000,
}


def arguments():
    parser = argparse.ArgumentParser(
        description="compare Otium collectors with the same reserved-memory budget"
    )
    parser.add_argument(
        "--budget-mib",
        "--physical-mib",
        dest="budget_mib",
        type=int,
        default=128,
        help="collector reservation-plus-metadata budget (default: 128 MiB)",
    )
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument(
        "--workload",
        action="append",
        choices=sorted(WORKLOADS),
        dest="workloads",
        help="workload to run; repeat the option to select more than one",
    )
    parser.add_argument("--csv", type=Path, help="also write raw samples as CSV")
    result = parser.parse_args()
    if result.budget_mib < 16:
        parser.error("--budget-mib must be at least 16")
    if result.warmups < 0 or result.runs < 1:
        parser.error("--warmups must be non-negative and --runs must be positive")
    return result


def heap_limits(budget_bytes):
    word_bytes = struct.calcsize("P")
    card_bytes = 32 * word_bytes
    metadata_per_card = 1 + 4 + 4 + word_bytes + 1
    mark_stack_bytes = MARK_STACK_ENTRIES * word_bytes

    semi = (budget_bytes - 4096) // 2
    available_old = budget_bytes - 2 * NURSERY_BYTES - mark_stack_bytes - 4096
    old = int(available_old / (1 + metadata_per_card / card_bytes))
    old -= old % card_bytes
    generational = old + NURSERY_BYTES
    return {"semi": semi, "gen": generational}


def build(root, collector, heap_max):
    build_dir = Path("build") / "gc-compare" / collector
    binary = root / build_dir / "gc-bench"
    command = [
        "make",
        "-B",
        f"BUILD={build_dir}",
        f"GC={collector}",
        "WITH_RAY=0",
        f"HEAP_MAX={heap_max}",
        str(build_dir / "gc-bench"),
    ]
    result = subprocess.run(command, cwd=root, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "build failed")
    return binary


def run_once(binary, workload, iterations):
    started = time.perf_counter_ns()
    result = subprocess.run(
        [binary, workload, str(iterations)],
        text=True,
        capture_output=True,
        check=False,
    )
    wall_ns = time.perf_counter_ns() - started
    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip() or "no output"
        raise RuntimeError(f"{binary.name} {workload}: {details}")
    try:
        sample = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"invalid benchmark output: {result.stdout!r}") from error
    sample["wall_ns"] = wall_ns
    return sample


def median(samples, key):
    return statistics.median(sample[key] for sample in samples)


def phase_total(sample):
    return sample["mutator_pause_total_ns"]


def phase_max(sample):
    return sample["mutator_pause_max_ns"]


def summarize(collector, workload, samples):
    workload_ns = median(samples, "elapsed_ns")
    allocated = median(samples, "allocated_bytes")
    return {
        "collector": collector,
        "workload": workload,
        "wall_ms": median(samples, "wall_ns") / 1e6,
        "workload_ms": workload_ns / 1e6,
        "mib_per_second": allocated / MIB / (workload_ns / 1e9),
        "pause_percent": statistics.median(
            phase_total(sample) / sample["elapsed_ns"] * 100 for sample in samples
        ),
        "max_pause_ms": max(phase_max(sample) for sample in samples) / 1e6,
        "collections": "/".join(
            str(round(median(samples, f"{phase}_collections")))
            for phase in ("full_copy", "minor", "major_sweep", "major_compact")
        ),
        "peak_mib": median(samples, "peak_used_bytes") / MIB,
        "reserved_mib": statistics.median(
            [
                sample["reserved_bytes"] + sample["metadata_bytes"]
                for sample in samples
            ]
        )
        / MIB,
    }


def print_table(rows):
    print(
        "| collector | workload | wall ms | workload ms | MiB/s | GC % | "
        "max pause ms | collections F/M/S/C | peak MiB | reserved+metadata MiB |"
    )
    print("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for row in rows:
        print(
            f"| {row['collector']} | {row['workload']} | {row['wall_ms']:.2f} | "
            f"{row['workload_ms']:.2f} | {row['mib_per_second']:.1f} | "
            f"{row['pause_percent']:.1f} | {row['max_pause_ms']:.3f} | "
            f"{row['collections']} | {row['peak_mib']:.2f} | "
            f"{row['reserved_mib']:.2f} |"
        )


def write_samples(path, samples):
    rows = []
    for (collector, workload), values in samples.items():
        for run, sample in enumerate(values, 1):
            rows.append({"collector": collector, "run": run, **sample})
    keys = sorted({key for row in rows for key in row})
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def main():
    args = arguments()
    root = Path(__file__).resolve().parent.parent
    budget_bytes = args.budget_mib * MIB
    limits = heap_limits(budget_bytes)
    workloads = args.workloads or list(WORKLOADS)
    try:
        binaries = {
            collector: build(root, collector, limits[collector])
            for collector in ("semi", "gen")
        }
        all_samples = {}
        for collector, binary in binaries.items():
            for workload in workloads:
                iterations = WORKLOADS[workload]
                for _ in range(args.warmups):
                    run_once(binary, workload, iterations)
                all_samples[(collector, workload)] = [
                    run_once(binary, workload, iterations) for _ in range(args.runs)
                ]
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"Collector memory budget: {args.budget_mib} MiB")
    print(f"Logical heap maxima: semi={limits['semi']} bytes, gen={limits['gen']} bytes")
    rows = [
        summarize(collector, workload, all_samples[(collector, workload)])
        for workload in workloads
        for collector in ("semi", "gen")
    ]
    print_table(rows)
    if args.csv is not None:
        write_samples(args.csv, all_samples)
    return 0


if __name__ == "__main__":
    sys.exit(main())
