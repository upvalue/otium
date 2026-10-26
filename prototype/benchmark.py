#!/usr/bin/env python3
"""Initial process-time baseline; includes startup and compilation, not steady state."""
import argparse
import json
import pathlib
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORKLOADS = {
    'fib30': (
        '(def (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))\n(println (fib 30))\n',
        'def fib(n):\n    return n if n < 2 else fib(n-1) + fib(n-2)\nprint(fib(30))\n',
        '832040'),
    'sum1000000': (
        '(println (let loop ((n 1000000) (sum 0)) (if (= n 0) sum (loop (- n 1) (+ sum n)))))\n',
        'n = 1000000\ntotal = 0\nwhile n:\n    total += n\n    n -= 1\nprint(total)\n',
        '500000500000'),
    'pairs50000': (
        '(let ((i 50000) (sum 0)) (while (> i 0) '
        '(let ((p (cons i (cons (- i 1) nil)))) (set! sum (+ sum (car p) (car (cdr p))))) '
        '(set! i (- i 1))) (println sum))\n',
        'i = 50000\ntotal = 0\nwhile i:\n    p = (i, (i-1, None))\n'
        '    total += p[0] + p[1][0]\n    i -= 1\nprint(total)\n',
        '2500000000'),
}
SCHEME = {
    'fib30': '(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))\n(display (fib 30))\n(newline)\n',
    'sum1000000': '(display (let loop ((n 1000000) (sum 0)) (if (= n 0) sum (loop (- n 1) (+ sum n)))))\n(newline)\n',
    'pairs50000': '(display (let loop ((i 50000) (sum 0)) (if (= i 0) sum '
    '(let ((p (cons i (cons (- i 1) \'())))) (loop (- i 1) (+ sum (car p) (car (cdr p))))))))\n(newline)\n',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repetitions', type=int, default=3)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error('repetitions must be positive')
    report = {'metric': 'startup-inclusive wall seconds', 'machine': platform.platform(),
              'python': sys.version.split()[0], 'repetitions': args.repetitions, 'results': []}
    commands = {
        'prototype-vm': [str(ROOT / 'prototype/otium-proto')],
        'otium-chez': [str(ROOT / 'otium'), '--runtime=chez', '--no-project'],
        'python': [sys.executable],
    }
    if shutil.which('scheme'):
        commands['chez-scheme'] = [shutil.which('scheme'), '--script']
    with tempfile.TemporaryDirectory(prefix='otium-benchmark-') as directory:
        for name, (otium, python, expected) in WORKLOADS.items():
            for implementation, command in commands.items():
                suffix = '.py' if implementation == 'python' else '.ss' if implementation == 'chez-scheme' else '.ot'
                path = pathlib.Path(directory) / (name + suffix)
                source = python if implementation == 'python' else (
                    '(import (chezscheme))\n' + SCHEME[name] if implementation == 'chez-scheme' else otium)
                path.write_text(source)
                samples = []
                for _ in range(args.repetitions):
                    start = time.perf_counter()
                    result = subprocess.run([*command, str(path)], cwd=ROOT,
                                            capture_output=True, text=True, timeout=60)
                    samples.append(time.perf_counter() - start)
                    if result.returncode or result.stdout.strip() != expected:
                        raise RuntimeError((name, implementation, result.stdout, result.stderr))
                report['results'].append({'workload': name, 'runtime': implementation,
                                          'samples': samples, 'median': statistics.median(samples)})
                print(f'{name} {implementation}: {statistics.median(samples):.6f}s', file=sys.stderr)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
