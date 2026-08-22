#!/usr/bin/env python3

"""Build Otium collectors under one reservation-plus-metadata budget."""

import argparse
import csv
import datetime
import json
import platform
import statistics
import struct
import subprocess
import sys
import time
from pathlib import Path


MIB = 1024 * 1024
NURSERY_BYTES = 2 * MIB
MARK_STACK_ENTRIES = 16384
COLLECTORS = ("semi", "gen", "gsgc")
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
        "--collector",
        action="append",
        choices=COLLECTORS,
        dest="collectors",
        help="collector to run; repeat the option to select more than one",
    )
    parser.add_argument(
        "--workload",
        action="append",
        choices=sorted(WORKLOADS),
        dest="workloads",
        help="workload to run; repeat the option to select more than one",
    )
    parser.add_argument("--csv", type=Path, help="also write raw samples as CSV")
    parser.add_argument(
        "--append-csv",
        action="store_true",
        help="append to --csv for longitudinal results instead of replacing it",
    )
    result = parser.parse_args()
    if result.budget_mib < 16:
        parser.error("--budget-mib must be at least 16")
    if result.warmups < 0 or result.runs < 1:
        parser.error("--warmups must be non-negative and --runs must be positive")
    if result.append_csv and result.csv is None:
        parser.error("--append-csv requires --csv")
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
    # GSGC reserves two new spaces and two old spaces initially; each old
    # space is twice the new-space size. Leave room for its remembered table
    # and heap structure. Its old spaces may grow if the live set requires it.
    gsgc_new = (budget_bytes - 64 * 1024) // 6
    gsgc_new -= gsgc_new % word_bytes
    return {
        "semi": {"heap_init": 1024 * 1024, "heap_max": semi},
        "gen": {"heap_init": 1024 * 1024, "heap_max": generational},
        "gsgc": {"heap_init": gsgc_new, "heap_max": budget_bytes},
    }


def build(root, collector, geometry):
    build_dir = Path("build") / "gc-compare" / collector
    binary = root / build_dir / "gc-bench"
    command = [
        "make",
        "-B",
        f"BUILD={build_dir}",
        f"GC={collector}",
        "WITH_RAY=0",
        f"HEAP_INIT={geometry['heap_init']}",
        f"HEAP_MAX={geometry['heap_max']}",
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


def revision(root):
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=root, text=True, capture_output=True, check=False
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def dirty_worktree(root):
    result = subprocess.run(
        ["git", "status", "--porcelain"],
        cwd=root,
        text=True,
        capture_output=True,
        check=False,
    )
    return result.returncode != 0 or bool(result.stdout.strip())


def write_samples(path, samples, metadata, append):
    rows = []
    for (collector, workload), values in samples.items():
        for run, sample in enumerate(values, 1):
            rows.append({**metadata, "collector": collector, "run": run, **sample})
    keys = sorted({key for row in rows for key in row})
    write_header = True
    mode = "w"
    if append and path.exists() and path.stat().st_size != 0:
        with path.open(newline="") as existing:
            existing_keys = next(csv.reader(existing), [])
        if existing_keys != keys:
            raise RuntimeError(
                f"cannot append {path}: columns differ from the existing CSV"
            )
        mode = "a"
        write_header = False
    with path.open(mode, newline="") as output:
        writer = csv.DictWriter(output, fieldnames=keys)
        if write_header:
            writer.writeheader()
        writer.writerows(rows)


def main():
    args = arguments()
    root = Path(__file__).resolve().parent.parent
    budget_bytes = args.budget_mib * MIB
    limits = heap_limits(budget_bytes)
    workloads = args.workloads or list(WORKLOADS)
    collectors = args.collectors or list(COLLECTORS)
    collected_at = datetime.datetime.now(datetime.timezone.utc).isoformat()
    try:
        binaries = {
            collector: build(root, collector, limits[collector]) for collector in collectors
        }
        all_samples = {}
        for collector, binary in binaries.items():
            for workload in workloads:
                iterations = WORKLOADS[workload]
                for _ in range(args.warmups):
                    run_once(binary, workload, iterations)
                all_samples[(collector, workload)] = [
                    {
                        **run_once(binary, workload, iterations),
                        "heap_init": limits[collector]["heap_init"],
                        "heap_max": limits[collector]["heap_max"],
                    }
                    for _ in range(args.runs)
                ]
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    print(f"Collector memory budget: {args.budget_mib} MiB")
    print(
        "Build geometry: "
        + ", ".join(
            f"{collector} heap_init={limits[collector]['heap_init']} "
            f"heap_max={limits[collector]['heap_max']}"
            for collector in collectors
        )
    )
    rows = [
        summarize(collector, workload, all_samples[(collector, workload)])
        for workload in workloads
        for collector in collectors
    ]
    print_table(rows)
    if args.csv is not None:
        metadata = {
            "budget_bytes": budget_bytes,
            "collected_at_utc": collected_at,
            "host": platform.node(),
            "machine": platform.machine(),
            "os": platform.platform(),
            "otium_dirty": dirty_worktree(root),
            "otium_revision": revision(root),
            "warmups": args.warmups,
            "measured_runs": args.runs,
        }
        try:
            write_samples(args.csv, all_samples, metadata, args.append_csv)
        except RuntimeError as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
