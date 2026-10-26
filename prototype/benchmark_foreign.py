#!/usr/bin/env python3
"""Warm integer foreign-call loop, including marshalling and language overhead."""
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
FIXTURE = ROOT / 'prototype/ffi_fixture.so'


def run(command):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, capture_output=True,
                            text=True, timeout=60)
    if result.returncode:
        raise RuntimeError((command, result.stderr))
    return json.loads(result.stdout)


def main():
    lua_library = available_library()
    if lua_library:
        subprocess.run(['make', '-C', str(ROOT / 'prototype'), 'bench_lua'],
                       check=True, stdout=sys.stderr)
    report = {'luajit_library': lua_library, 'metric': 'warm million-call loop wall seconds', 'warmups': 2, 'samples_per_kernel': 5,
              'luajit_warmup_policy': 'At least two calls; discard samples when a new trace starts; limit 100 calls',
              'machine': platform.platform(), 'python': sys.version.split()[0],
              'cpu_affinity': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
              'results': []}
    report['results'].append(run([ROOT / 'prototype/bench_runtime', 'ffi1000000']))
    with tempfile.TemporaryDirectory(prefix='otium-foreign-benchmark-') as directory:
        path = pathlib.Path(directory) / 'kernel.py'
        path.write_text(f'''
import ctypes, json, resource, time
library = ctypes.CDLL({str(FIXTURE)!r})
echo = library.echo_int
echo.argtypes = [ctypes.c_int]
echo.restype = ctypes.c_int
def kernel(n):
    total = 0
    while n:
        total += echo(n)
        n -= 1
    return total
samples = []
for i in range(7):
    start = time.perf_counter()
    answer = kernel(1000000)
    elapsed = time.perf_counter() - start
    assert answer == 500000500000
    if i >= 2:
        samples.append(elapsed)
print(json.dumps({{'runtime': 'python-ctypes', 'samples': samples,
                  'peak_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss}}))
''')
        report['results'].append(run([sys.executable, path]))
        if lua_library:
            for enabled in (False, True):
                report['results'].append(run_lua(ROOT, pathlib.Path(directory), lua_library,
                                                'ffi1000000', 1000000, 500000500000, enabled))
        if shutil.which('scheme'):
            path = pathlib.Path(directory) / 'kernel.ss'
            path.write_text(f'''
(import (chezscheme))
(load-shared-object "{FIXTURE}")
(define echo (foreign-procedure "echo_int" (int) int))
(define (kernel n)
  (let loop ((n n) (total 0))
    (if (= n 0) total (loop (- n 1) (+ total (echo n))))))
(define (clock)
  (let ((t (current-time 'time-monotonic)))
    (+ (time-second t) (/ (time-nanosecond t) 1e9))))
(display "{{\\"runtime\\":\\"chez-scheme\\",\\"samples\\":[")
(do ((i 0 (+ i 1))) ((= i 7))
  (let* ((start (clock)) (answer (kernel 1000000)) (elapsed (- (clock) start)))
    (unless (= answer 500000500000) (error 'benchmark "wrong result"))
    (when (>= i 2)
      (when (> i 2) (display ","))
      (display elapsed))))
(display "]}}")
(newline)
''')
            report['results'].append(run([shutil.which('scheme'), '--script', path]))
    for result in report['results']:
        result['median'] = statistics.median(result['samples'])
        print(f"{result['runtime']}: {result['median']:.6f}s", file=sys.stderr)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
