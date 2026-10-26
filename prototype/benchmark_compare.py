#!/usr/bin/env python3
"""Compare prototype benchmark executables, each with its matching native headers."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import statistics
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=pathlib.Path)
    parser.add_argument('--after', type=pathlib.Path, default=ROOT / 'prototype/bench_runtime')
    parser.add_argument('--runs', type=int, default=5)
    parser.add_argument('--mode', choices=['vm', 'native'], default='native')
    parser.add_argument('--cpu', type=int, help='pin this process and its children to an allowed CPU')
    parser.add_argument('--workloads', nargs='+', default=[
        'optional100000', 'rest100000', 'apply100000', 'fib30'])
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    if args.cpu is not None:
        if not hasattr(os, 'sched_setaffinity') or args.cpu not in os.sched_getaffinity(0):
            parser.error('--cpu must name an allowed CPU on a host supporting affinity')
        os.sched_setaffinity(0, {args.cpu})
    executables = {'before': args.before.resolve(), 'after': args.after.resolve()}
    report = {'mode': args.mode, 'runs': args.runs, 'samples_per_run': 5, 'warmups_per_run': 2,
              'machine': platform.platform(),
              'cpu_affinity': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
              'executables': {}, 'results': {}}
    for label, path in executables.items():
        report['executables'][label] = {'path': str(path),
                                       'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    for workload in args.workloads:
        samples = {label: [] for label in executables}
        for repetition in range(args.runs):
            order = list(executables)
            if repetition % 2:
                order.reverse()
            for label in order:
                command = [str(executables[label]), workload]
                if args.mode == 'native':
                    command.append('--native')
                result = subprocess.run(command,
                                        cwd=ROOT, capture_output=True, text=True, timeout=60)
                if result.returncode:
                    raise RuntimeError((label, workload, result.stderr))
                measured = json.loads(result.stdout)
                assert len(measured['samples']) == 5
                samples[label].extend(measured['samples'])
        medians = {label: statistics.median(values) for label, values in samples.items()}
        report['results'][workload] = {'samples': samples, 'medians': medians}
        print(f'{workload}: {medians}', file=sys.stderr)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
