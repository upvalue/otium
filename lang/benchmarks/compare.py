#!/usr/bin/env python3
"""Compare the C runtime with the Chez runtime using checked workloads."""

import argparse
import datetime
import json
import os
import platform
import shutil
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CHEZ_ROOT = ROOT / "chez"
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


def program(name, engine):
    setup, expression, _ = WORKLOADS[name]
    if name == "startup":
        return "(println 0)\n"
    if engine == "c-runtime":
        clock = "(def (bench-clock) (current-jiffy))"
        collections = """(def bench-table table)
          (def bench-put! put!) (def bench-get get)"""
        imports = "(require (callee :as c))"
        callback = "(def callback (fn (x) (c/inc x)))"
        reload = "(def (bench-reload) (require (callee :as c :reload)))"
    else:
        clock = """(require chez)
          (def (bench-clock)
          (let ((t (chez/current-time 'time-monotonic)))
            (+ (* (chez/time-second t) 1000000000) (chez/time-nanosecond t))))"""
        collections = """(def bench-table table)
          (def bench-put! put!) (def bench-get get)"""
        imports = "(require callee :as c)"
        callback = "(def callback (live 'c/inc))"
        # Loader directives are not accepted in Chez function bodies.
        reload = """(chez/import (only (otium runtime) evaluate))
          (def (bench-reload) (evaluate '(require callee :as c :reload)))"""
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
    parser.add_argument("--otium", type=Path, required=True,
                        help="C runtime executable; build before running")
    parser.add_argument("--chez", default=os.environ.get("CHEZ", "chez"))
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--build-label", default="unspecified")
    parser.add_argument("--only", nargs="+", choices=WORKLOADS)
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0 or args.timeout <= 0:
        parser.error("runs and timeout must be positive; warmups must be nonnegative")
    chez = shutil.which(args.chez)
    if not chez or not args.otium.resolve().is_file():
        parser.error("both runtime executables must exist")
    version = subprocess.run([chez, "--version"], capture_output=True, text=True, check=True)
    report = {
        "date": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "platform": platform.platform(), "machine": platform.machine(),
        "chez_version": (version.stdout + version.stderr).strip(),
        "c_runtime": str(args.otium.resolve()), "build_label": args.build_label,
        "runs": args.runs, "warmups": args.warmups,
        "method": "Fresh processes, alternating engine order; wall includes startup and compilation; work excludes setup, includes GC. No explicit collection. All results checked.",
        "benchmarks": {},
    }
    print(f"C runtime ({args.build_label}) vs Otium on Chez {report['chez_version']}")
    print(f"{args.runs} measured runs, {args.warmups} warmups; medians in milliseconds")
    print(f"{'workload':<19} {'C work':>10} {'Chez work':>10} {'speedup':>9} {'C wall':>10} {'Chez wall':>10}", flush=True)
    with tempfile.TemporaryDirectory(prefix="otium-compare-") as temp:
        directory = Path(temp)
        (directory / "callee.scm").write_text("(ns callee)\n(def (inc x) (+ x 1))\n")
        for name in args.only or WORKLOADS:
            samples = {engine: [] for engine in ("c-runtime", "chez")}
            commands = {}
            for engine in samples:
                path = directory / f"{name}-{engine}.scm"
                path.write_text(program(name, engine))
                commands[engine] = ([str(args.otium.resolve()), "--path", temp, str(path)]
                    if engine == "c-runtime" else
                    [chez, "--libdirs", str(CHEZ_ROOT), "--script",
                     str(CHEZ_ROOT / "main.ss"), "-L", temp, str(path)])
            for iteration in range(args.warmups + args.runs):
                order = list(samples)
                if iteration % 2:
                    order.reverse()
                for engine in order:
                    sample = run(commands[engine], temp, WORKLOADS[name][2],
                                 name == "startup", args.timeout)
                    if iteration >= args.warmups:
                        samples[engine].append(sample)
            summary = {}
            for engine, values in samples.items():
                summary[engine] = {"samples": values}
                for metric in ("wall_ms", "work_ms"):
                    numbers = [v[metric] for v in values if v[metric] is not None]
                    summary[engine][metric] = ({"median": statistics.median(numbers),
                        "min": min(numbers), "max": max(numbers)} if numbers else None)
            report["benchmarks"][name] = summary
            c, s = summary.values()
            work = (f"{c['work_ms']['median']:10.3f} {s['work_ms']['median']:10.3f} "
                    f"{c['work_ms']['median'] / s['work_ms']['median']:8.1f}x"
                    if c["work_ms"] and s["work_ms"]["median"] else
                    f"{'-':>10} {'-':>10} {'-':>9}")
            print(f"{name:<19} {work} {c['wall_ms']['median']:10.3f} {s['wall_ms']['median']:10.3f}", flush=True)
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
