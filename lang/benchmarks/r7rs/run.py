#!/usr/bin/env python3

"""Run Otium benchmark ports and optional upstream Guile references."""

import argparse
import csv
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


CSV_LINE = re.compile(r"^\+!CSVLINE!\+([^,]+),([^,]+),(.+)$", re.MULTILINE)


def vendor_available(root):
    return (root / "vendor" / "src" / "common.scm").is_file()


def print_vendor_error():
    print(
        "error: r7rs-benchmarks submodule is not initialized; run "
        "git submodule update --init benchmarks/r7rs/vendor",
        file=sys.stderr,
    )


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="path to the Otium executable")
    parser.add_argument("benchmarks", nargs="*", help="ports to run; default: all")
    parser.add_argument("--runs", type=int, default=1, help="samples per benchmark")
    parser.add_argument("--timeout", type=float, default=300.0, help="seconds per sample")
    parser.add_argument(
        "--reference", choices=["guile"], help="also run unmodified upstream source"
    )
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return args


def git_value(cwd, *args):
    result = subprocess.run(
        ["git", "-C", cwd, *args], text=True, capture_output=True, check=False
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def parse_result(stdout, returncode):
    match = CSV_LINE.search(stdout)
    if returncode != 0:
        return "crashed", None, None, None, f"process exited with status {returncode}"
    if not match:
        return "crashed", None, None, None, "missing CSV line"
    implementation, name, value = match.groups()
    if value == "INCORRECT":
        return "incorrect", implementation, name, None, ""
    try:
        return "ok", implementation, name, float(value), ""
    except ValueError:
        return "crashed", implementation, name, None, f"bad reported time: {value}"


def execute(command, timeout, stdin=None, env=None):
    started = time.perf_counter()
    try:
        result = subprocess.run(
            command,
            input=stdin,
            text=True,
            capture_output=True,
            check=False,
            timeout=timeout,
            env=env,
        )
    except subprocess.TimeoutExpired as error:
        return "timeout", timeout, "", (error.stderr or "")
    wall = time.perf_counter() - started
    return result.returncode, wall, result.stdout, result.stderr


def otium_program(root, name, implementation, input_text):
    library = (root / "lib" / "benchmark.scm").read_text()
    port = (root / "ports" / f"{name}.scm").read_text()
    return (
        f'(define *benchmark-implementation* "{implementation}")\n'
        f"(define *benchmark-inputs* '(\n{input_text}\n))\n"
        f"{library}\n{port}\n(benchmark-main)\n"
    )


def upstream_program(root, name):
    parts = [
        root / "vendor" / "src" / "Guile-prelude.scm",
        root / "vendor" / "src" / f"{name}.scm",
        root / "vendor" / "src" / "common.scm",
        root / "vendor" / "src" / "Guile-postlude.scm",
        root / "vendor" / "src" / "common-postlude.scm",
    ]
    return "\n".join(path.read_text() for path in parts if path.exists())


def result_row(meta, variant, parity, sample, status, wall, reported=None, reason=""):
    return {
        **meta,
        "variant": variant,
        "parity": parity,
        "sample": sample,
        "reported_seconds": "" if reported is None else f"{reported:.9f}",
        "wall_seconds": "" if wall is None else f"{wall:.9f}",
        "status": status,
        "reason": reason.replace("\n", " ")[:240],
    }


def main():
    args = parse_args()
    root = Path(__file__).resolve().parent
    if not vendor_available(root):
        print_vendor_error()
        return 2
    repo = root.parents[1]
    binary = args.binary.resolve()
    manifest = json.loads((root / "manifest.json").read_text())
    selected = args.benchmarks or list(manifest["benchmarks"])
    unknown = [name for name in selected if name not in manifest["benchmarks"]]
    if unknown:
        print(f"error: unknown benchmark: {unknown[0]}", file=sys.stderr)
        return 2
    if not binary.is_file():
        print(f"error: executable not found: {binary}", file=sys.stderr)
        return 2

    otium_commit = git_value(repo, "rev-parse", "--short", "HEAD")
    dirty = bool(git_value(repo, "status", "--porcelain"))
    implementation = f"otium-{otium_commit}{'-dirty' if dirty else ''}"
    host = f"{platform.system()} {platform.release()} {platform.machine()}"
    rows = []
    had_failure = False

    guile = shutil.which("guile") if args.reference == "guile" else None
    with tempfile.TemporaryDirectory(prefix="otium-r7rs-") as temp_dir:
        temp = Path(temp_dir)
        for name in selected:
            entry = manifest["benchmarks"][name]
            input_path = root / "vendor" / "inputs" / f"{name}.input"
            input_text = input_path.read_text()
            input_id = hashlib.sha256(input_path.read_bytes()).hexdigest()[:12]
            heap_max = entry.get("heap_max", 64 * 1024 * 1024)
            flags = [
                "--max-depth",
                "2000",
                "--stack-slots",
                "65536",
                "--heap-max",
                str(heap_max),
            ]
            program_path = temp / f"{name}-otium.scm"
            program_path.write_text(otium_program(root, name, implementation, input_text))

            for sample in range(1, args.runs + 1):
                code, wall, stdout, stderr = execute(
                    [str(binary), *flags, str(program_path)], args.timeout
                )
                if code == "timeout":
                    status, impl, bench_name, reported, reason = (
                        "timeout",
                        implementation,
                        name,
                        None,
                        f"timed out after {args.timeout:g}s",
                    )
                else:
                    status, impl, bench_name, reported, reason = parse_result(stdout, code)
                    if status == "crashed":
                        reason = stderr or stdout or reason
                meta = {
                    "implementation": impl or implementation,
                    "benchmark": bench_name or name,
                    "input": input_id,
                    "platform": host,
                    "runtime_flags": " ".join(flags),
                    "upstream_commit": manifest["upstream_commit"][:12],
                    "otium_commit": otium_commit,
                }
                rows.append(
                    result_row(meta, "port", entry["parity"], sample, status, wall, reported, reason)
                )
                print(f"{name} port sample {sample}: {status}")
                had_failure |= status != "ok"

            if args.reference:
                if not guile:
                    meta = {
                        "implementation": "guile-unavailable",
                        "benchmark": name,
                        "input": input_id,
                        "platform": host,
                        "runtime_flags": "",
                        "upstream_commit": manifest["upstream_commit"][:12],
                        "otium_commit": otium_commit,
                    }
                    rows.append(
                        result_row(meta, "upstream", "A", 0, "unsupported", None, reason="guile not found")
                    )
                    continue
                source_path = temp / f"{name}-guile.scm"
                source_path.write_text(upstream_program(root, name))
                env = os.environ.copy()
                env["GC_INITIAL_HEAP_SIZE"] = "100000000"
                for sample in range(1, args.runs + 1):
                    code, wall, stdout, stderr = execute(
                        [guile, str(source_path)], args.timeout, stdin=input_text, env=env
                    )
                    if code == "timeout":
                        status, impl, bench_name, reported, reason = (
                            "timeout",
                            "guile",
                            name,
                            None,
                            f"timed out after {args.timeout:g}s",
                        )
                    else:
                        status, impl, bench_name, reported, reason = parse_result(stdout, code)
                        if status == "crashed":
                            reason = stderr or stdout or reason
                    meta = {
                        "implementation": impl or "guile",
                        "benchmark": bench_name or name,
                        "input": input_id,
                        "platform": host,
                        "runtime_flags": "GC_INITIAL_HEAP_SIZE=100000000",
                        "upstream_commit": manifest["upstream_commit"][:12],
                        "otium_commit": otium_commit,
                    }
                    rows.append(
                        result_row(meta, "upstream", "A", sample, status, wall, reported, reason)
                    )
                    print(f"{name} upstream sample {sample}: {status}")

    fields = [
        "implementation",
        "benchmark",
        "variant",
        "parity",
        "input",
        "sample",
        "reported_seconds",
        "wall_seconds",
        "status",
        "reason",
        "platform",
        "runtime_flags",
        "upstream_commit",
        "otium_commit",
    ]
    with (root / "results.csv").open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    status_lines = [
        "# Benchmark status",
        "",
        "| benchmark | variant | parity | sample | status | reported | wall |",
        "|---|---|---:|---:|---|---:|---:|",
    ]
    for row in rows:
        status_lines.append(
            f"| {row['benchmark']} | {row['variant']} | {row['parity']} | "
            f"{row['sample']} | {row['status']} | {row['reported_seconds']} | "
            f"{row['wall_seconds']} |"
        )
    (root / "STATUS.md").write_text("\n".join(status_lines) + "\n")
    return 1 if had_failure else 0


if __name__ == "__main__":
    sys.exit(main())
