#!/usr/bin/env python3
"""Measure warm kernels separately from startup and native compilation."""
import argparse
import json
import os
import pathlib
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
from benchmark_lua import available_library, run_lua

ROOT = pathlib.Path(__file__).resolve().parent.parent
CASES = {
    'float1000000': (1000000, 500000.0,
        'def kernel(n):\n    total = 0.0\n    step = 0.5\n    while n > 0:\n        total += step\n        n -= 1\n    return total\n',
        '(define (kernel n) (let ((step 0.5)) (let loop ((n n) (total 0.0)) '
        '(if (= n 0) total (loop (- n 1) (+ total step))))))'),
    'optional100000': (100000, 5000350000,
        'def sum_optional(a, b=1, c=2):\n    return a + b + c\n'
        'def kernel(n):\n    total = 0\n    while n > 0:\n        total += sum_optional(n)\n        n -= 1\n    return total\n',
        '(define sum-optional (case-lambda ((a) (sum-optional a 1 2)) '
        '((a b) (sum-optional a b 2)) ((a b c) (+ a b c)))) '
        '(define (kernel n) (let loop ((n n) (total 0)) '
        '(if (= n 0) total (loop (- n 1) (+ total (sum-optional n))))))'),
    'apply100000': (100000, 5000350000,
        'def sum_three(a, b, c):\n    return a + b + c\n'
        'def kernel(n):\n    total = 0\n    tail = [1, 2]\n    while n > 0:\n        total += sum_three(n, *tail)\n        n -= 1\n    return total\n',
        '(define (sum-three a b c) (+ a b c)) '
        '(define (kernel n) (let ((tail (list 1 2))) (let loop ((n n) (total 0)) '
        '(if (= n 0) total (loop (- n 1) (+ total (apply sum-three n tail)))))))'),
    'rest100000': (100000, 5000350000,
        'def sum_rest(a, *rest):\n    return a + rest[0] + rest[1]\n'
        'def kernel(n):\n    total = 0\n    while n > 0:\n        total += sum_rest(n, 1, 2)\n        n -= 1\n    return total\n',
        '(define (sum-rest a . rest) (+ a (car rest) (cadr rest))) '
        '(define (kernel n) (let loop ((n n) (total 0)) '
        '(if (= n 0) total (loop (- n 1) (+ total (sum-rest n 1 2))))))'),
    'objectkeys5000': (5000, 24995000,
        'def kernel(n):\n    keys = []\n    t = {}\n    i = 0\n    total = 0\n    while i < n:\n'
        '        key = object()\n        keys.append(key)\n        t[key] = i * 2\n        i += 1\n'
        '    i = 0\n    while i < n:\n        total += t[keys[i]]\n        i += 1\n    return total\n',
        '(define (kernel n) (let ((keys (make-vector 4)) (capacity 4) (t (make-eq-hashtable)) (total 0)) '
        '(do ((i 0 (+ i 1))) ((= i n)) '
        '(when (= i capacity) (let ((grown (make-vector (* capacity 2)))) '
        '(do ((j 0 (+ j 1))) ((= j i)) (vector-set! grown j (vector-ref keys j))) '
        '(set! keys grown) (set! capacity (* capacity 2)))) (let ((key (make-vector 1 #f))) '
        '(vector-set! keys i key) (hashtable-set! t key (* i 2)))) '
        '(do ((i 0 (+ i 1))) ((= i n)) (set! total (+ total (hashtable-ref t (vector-ref keys i) #f)))) total))'),
    'tables10000': (10000, 99990000,
        'def kernel(n):\n    t = {}\n    i = 0\n    while i < n:\n        t[i] = i * 2\n        i += 1\n'
        '    i = 0\n    total = 0\n    while i < n:\n        total += t[i]\n        i += 1\n    return total\n',
        '(define (kernel n) (let ((t (make-eqv-hashtable)) (total 0)) '
        '(do ((i 0 (+ i 1))) ((= i n)) (hashtable-set! t i (* i 2))) '
        '(do ((i 0 (+ i 1))) ((= i n)) (set! total (+ total (hashtable-ref t i #f)))) total))'),
    'arrays50000': (50000, 1249975000,
        'def kernel(n):\n    a = []\n    i = 0\n    while i < n:\n        a.append(i)\n        i += 1\n'
        '    i = 0\n    total = 0\n    while i < n:\n        total += a[i]\n        i += 1\n    return total\n',
        '(define (kernel n) (let ((a (make-vector 4)) (capacity 4) (total 0)) '
        '(do ((i 0 (+ i 1))) ((= i n)) '
        '(when (= i capacity) (let ((grown (make-vector (* capacity 2)))) '
        '(do ((j 0 (+ j 1))) ((= j i)) (vector-set! grown j (vector-ref a j))) '
        '(set! a grown) (set! capacity (* capacity 2)))) (vector-set! a i i)) '
        '(do ((i 0 (+ i 1))) ((= i n)) (set! total (+ total (vector-ref a i)))) total))'),
    'fib30': (30, 832040,
              'def kernel(n):\n    return n if n < 2 else kernel(n-1) + kernel(n-2)\n',
              '(define (kernel n) (if (< n 2) n (+ (kernel (- n 1)) (kernel (- n 2)))))'),
    'sum1000000': (1000000, 500000500000,
                   'def kernel(n):\n    total = 0\n    while n:\n        total += n\n        n -= 1\n    return total\n',
                   '(define (kernel n) (let loop ((n n) (total 0)) (if (= n 0) total (loop (- n 1) (+ total n)))))'),
    'pairs50000': (50000, 2500000000,
                  'def kernel(n):\n    total = 0\n    while n:\n        p = (n, (n-1, None))\n'
                  '        total += p[0] + p[1][0]\n        n -= 1\n    return total\n',
                  '(define (kernel n) (let loop ((n n) (total 0)) (if (= n 0) total '
                  '(let ((p (cons n (cons (- n 1) \'())))) (loop (- n 1) (+ total (car p) (car (cdr p))))))))'),
}


def run(command):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True,
                            text=True, timeout=60)
    if result.returncode:
        raise RuntimeError((command, result.stdout, result.stderr))
    return json.loads(result.stdout)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workloads', nargs='+', choices=list(CASES), default=list(CASES))
    parser.add_argument('--cpu', type=int, help='pin this process and its children to an allowed CPU')
    args = parser.parse_args()
    if args.cpu is not None:
        if not hasattr(os, 'sched_setaffinity') or args.cpu not in os.sched_getaffinity(0):
            parser.error('--cpu must name an allowed CPU on a host supporting affinity')
        os.sched_setaffinity(0, {args.cpu})
    lua_library = available_library()
    if lua_library:
        subprocess.run(['make', '-C', str(ROOT / 'prototype'), 'bench_lua'],
                       check=True, stdout=sys.stderr)
    report = {'workloads': args.workloads, 'luajit_library': lua_library, 'metric': 'warm kernel wall seconds', 'warmups': 2, 'samples_per_kernel': 5,
              'luajit_warmup_policy': 'At least two calls; discard samples when a new trace starts; limit 100 calls',
              'machine': platform.platform(), 'python': sys.version.split()[0],
              'cpu_affinity': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
              'results': []}
    with tempfile.TemporaryDirectory(prefix='otium-steady-') as directory:
        for workload in args.workloads:
            argument, expected, python, scheme = CASES[workload]
            for native in (False, True):
                command = [ROOT / 'prototype/bench_runtime', workload]
                if native:
                    command.append('--native')
                report['results'].append(run(command))
            python_path = pathlib.Path(directory) / 'kernel.py'
            python_path.write_text('import json, resource, time\n' + python + f'''
samples = []
for i in range(7):
    start = time.perf_counter()
    answer = kernel({argument})
    elapsed = time.perf_counter() - start
    assert answer == {expected}
    if i >= 2:
        samples.append(elapsed)
print(json.dumps({{'workload': {workload!r}, 'runtime': 'python', 'samples': samples,
                  'peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}}))
''')
            report['results'].append(run([sys.executable, python_path]))
            if lua_library:
                for enabled in (False, True):
                    report['results'].append(run_lua(ROOT, pathlib.Path(directory), lua_library,
                                                    workload, argument, expected, enabled))
            if shutil.which('scheme'):
                scheme_path = pathlib.Path(directory) / 'kernel.ss'
                scheme_path.write_text('(import (chezscheme))\n' + scheme + f'''
(define (clock)
  (let ((t (current-time 'time-monotonic)))
    (+ (time-second t) (/ (time-nanosecond t) 1e9))))
(display "{{\\"workload\\":\\"{workload}\\",\\"runtime\\":\\"chez-scheme\\",\\"samples\\":[")
(do ((i 0 (+ i 1))) ((= i 7))
  (let* ((start (clock)) (answer (kernel {argument})) (elapsed (- (clock) start)))
    (unless (= answer {expected}) (error 'benchmark "wrong result"))
    (when (>= i 2)
      (when (> i 2) (display ","))
      (display elapsed))))
(display "]}}")
(newline)
''')
                report['results'].append(run([shutil.which('scheme'), '--script', scheme_path]))
    for entry in report['results']:
        entry['median'] = statistics.median(entry['samples'])
        print(f"{entry['workload']} {entry['runtime']}: {entry['median']:.6f}s", file=sys.stderr)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
