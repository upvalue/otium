#!/usr/bin/env python3
"""Time checked workloads from inside the program as well as from outside it."""

import argparse
import datetime
import json
import platform
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
import sys

# Repository modules, for running this file directly.
sys.path[:0] = [str(Path(__file__).resolve().parents[1] / part) for part in ('', 'tools')]
from otium_tools import command, metadata, run_directory


WORKLOADS = {
    "startup": ("", "0", 0),
    "tail-loop": (
        "",
        "(let loop ((n 3000000) (sum 0)) "
        "(if (= n 0) sum (loop (- n 1) (+ sum n))))",
        4500001500000,
    ),
    "fib-28": (
        "(def (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))",
        "(fib 28)",
        317811,
    ),
    "list-allocation": (
        "",
        """(let repeat ((round 0) (total 0))
          (if (= round 20) total
            (let ((xs (let build ((n 50000) (xs '()))
                        (if (= n 0) xs (build (- n 1) (cons n xs))))))
              (let walk ((xs xs) (sum 0))
                (if (null? xs) (repeat (+ round 1) (+ total sum))
                    (walk (cdr xs) (+ sum (car xs))))))))""",
        25000500000,
    ),
    "tables": (
        "",
        """(let ((items (bench-table)))
          (let fill ((n 0))
            (if (< n 100000)
                (begin (bench-put! items n (* n 2)) (fill (+ n 1))) #f))
          (let read-back ((n 0) (sum 0))
            (if (= n 100000) sum
                (read-back (+ n 1) (+ sum (bench-get items n))))))""",
        9999900000,
    ),
    "namespace-calls": (
        "",
        "(let loop ((n 1000000) (sum 0)) "
        "(if (= n 0) sum (loop (- n 1) (c/inc sum))))",
        1000000,
    ),
    "live-callback": (
        "",
        "(let loop ((n 1000000) (sum 0)) "
        "(if (= n 0) sum (loop (- n 1) (callback sum))))",
        1000000,
    ),
    "reload-20": (
        "",
        "(let loop ((n 20)) "
        "(if (= n 0) (c/inc 0) (begin (bench-reload) (loop (- n 1)))))",
        1,
    ),
}


def program(name):
    setup, expression, _ = WORKLOADS[name]
    if name == "startup":
        return "(println 0)\n"
    clock = "(def (bench-clock) (current-jiffy))"
    collections = """(def bench-table table)
      (def bench-put! put!) (def bench-get get)"""
    imports = "(require (callee :as c))"
    callback = "(def callback (fn (x) (c/inc x)))"
    reload = "(def (bench-reload) (eval '(require (callee :as c :reload))))"
    parts = [clock, setup]
    if name == "tables":
        parts.append(collections)
    if name in ("namespace-calls", "live-callback", "reload-20"):
        parts.append(imports)
    if name == "live-callback":
        parts.append(callback)
    if name == "reload-20":
        parts.append(reload)
    parts.append(f"""(let ((started (bench-clock)))
      (let ((answer {expression}))
        (let ((elapsed (- (bench-clock) started)))
          (println answer) (println elapsed))))""")
    return "\n".join(parts) + "\n"


def run(command, cwd, expected, startup, timeout):
    started = time.perf_counter_ns()
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                            timeout=timeout)
    wall_ms = (time.perf_counter_ns() - started) / 1e6
    if result.returncode:
        raise RuntimeError(f"{command}: {result.stderr or result.stdout}")
    lines = result.stdout.splitlines()
    if len(lines) != (1 if startup else 2) or int(lines[0]) != expected:
        raise RuntimeError(f"incorrect result: {command}: {result.stdout!r}")
    return {"wall_ms": wall_ms,
            "work_ms": None if startup else int(lines[1]) / 1e6}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--otium", type=Path, default=None,
                        help="executable to run (default: the ./otium launcher)")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--build-label", default="unspecified")
    parser.add_argument("--only", nargs="+", choices=WORKLOADS)
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0 or args.timeout <= 0:
        parser.error("runs and timeout must be positive; warmups must be nonnegative")
    otium = command(executable=args.otium and str(args.otium.absolute()))
    if not Path(otium[0]).is_file():
        parser.error(f"executable not found: {otium[0]}")
    report = {
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "platform": platform.platform(), "machine": platform.machine(),
        "executable": otium, "build_label": args.build_label,
        "runs": args.runs, "warmups": args.warmups,
        "method": "Fresh processes; wall includes startup and compilation; work excludes setup, includes GC. No explicit collection. All results checked.",
        "benchmarks": {},
    }
    print(f"{args.runs} measured runs, {args.warmups} warmups; medians in milliseconds")
    print(f"{'workload':<19} {'work':>10} {'wall':>10}", flush=True)
    measured_commands = {}
    with tempfile.TemporaryDirectory(prefix="otium-timed-") as temp:
        directory = Path(temp)
        (directory / "callee.ot").write_text("(ns callee)\n(def (inc x) (+ x 1))\n")
        for name in args.only or WORKLOADS:
            path = directory / f"{name}.ot"
            path.write_text(program(name))
            measured_commands[name] = [*otium, "--path", temp, str(path)]
            samples = []
            for iteration in range(args.warmups + args.runs):
                sample = run(measured_commands[name], temp, WORKLOADS[name][2],
                             name == "startup", args.timeout)
                if iteration >= args.warmups:
                    samples.append(sample)
            summary = {"samples": samples}
            for metric in ("wall_ms", "work_ms"):
                numbers = [v[metric] for v in samples if v[metric] is not None]
                summary[metric] = ({"median": statistics.median(numbers),
                    "min": min(numbers), "max": max(numbers)} if numbers else None)
            report["benchmarks"][name] = summary
            work = f"{summary['work_ms']['median']:10.3f}" if summary["work_ms"] else f"{'-':>10}"
            print(f"{name:<19} {work} {summary['wall_ms']['median']:10.3f}", flush=True)
    report['provenance'] = metadata(measured_commands, build_label=args.build_label)
    output = args.output or run_directory('timed') / 'results.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n")
    print(f'Results: {output}')


if __name__ == "__main__":
    main()
