#!/usr/bin/env python3
"""Benchmark C Otium generic calls against equivalent cond and preselected calls."""
import argparse
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('binary')
parser.add_argument('--calls', type=int, default=30000)
args = parser.parse_args()
if args.calls < 1:
    parser.error('--calls must be positive')
binary = str(Path(args.binary).resolve())
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(['positions', 'exact_methods', 'strategy', 'calls', 'median_ns_per_call', 'checksum'])
for nx, ny, binary_dispatch in [(4, 1, False), (16, 1, False), (64, 1, False),
                               (2, 2, True), (4, 4, True), (8, 8, True)]:
    forms = ['(require (otium.generic :as g))', '(define-generic (operation x y))',
             '(define-method (operation x y) -1)', '(def methods [])']
    for a in range(nx):
        forms.append(f'(define-type W{a})')
        if binary_dispatch:
            forms.append(f'(define-method (operation (x W{a}) y) -2)')
    for b in range(ny):
        forms.append(f'(define-type E{b})')
    for a in range(nx):
        for b in range(ny):
            offset = a * ny + b
            body = f"(+ (g/field-ref x 'payload) (g/field-ref y 'payload) {offset})"
            event = f'(y E{b})' if binary_dispatch else 'y'
            forms.append(f'(define-method (operation (x W{a}) {event}) {body})')
            forms.append(f'(push! methods (fn (x y) {body}))')
    for name, reader in [('cond-descriptor', 'g/dispatch-type'), ('cond-map-field', 'g/field-ref')]:
        def read(x):
            return f'({reader} {x})' if reader == 'g/dispatch-type' else f"({reader} {x} 'kind)"
        clauses = []
        for a in range(nx):
            for b in range(ny):
                predicate = f'(and (eq? tx W{a}) (eq? ty E{b}))' if binary_dispatch else f'(eq? tx W{a})'
                clauses.append(f"({predicate} (+ (g/field-ref x 'payload) (g/field-ref y 'payload) {a * ny + b}))")
        forms.append(f'(def ({name} index x y) (let ((tx {read("x")}) (ty {read("y")})) (cond {" ".join(clauses)} (else -1))))')
    forms.extend(['(def lefts [])', '(def rights [])', '(def selected [])'])
    # A deterministic permutation distributes types while payloads vary by sample.
    for i in range(128):
        a, b = (i * 17 + 3) % nx, (i * 11 + i // nx + 1) % ny
        forms.extend([f"(push! lefts (g/make W{a} 'kind W{a} 'payload {i * 7 + 1}))",
                      f"(push! rights (g/make E{b} 'kind E{b} 'payload {i * 13 + 2}))",
                      f'(push! selected (get methods {a * ny + b}))'])
    forms.append(f'''
(def (run op calls)
  (let ((n 0) (checksum 0))
    (while (< n calls)
      (let ((i (modulo n 128)))
        (set! checksum (+ checksum (op i (get lefts i) (get rights i)))))
      (set! n (+ n 1)))
    checksum))
(def (preselected i x y) ((get selected i) x y))
(def (generic i x y) (operation x y))
(def expected (run preselected {args.calls}))
(def (measure label op)
  (run op 2000)
  (dotimes (r 5)
    (let ((start (current-jiffy)) (checksum (run op {args.calls})))
      (unless (= checksum expected) (error "benchmark checksum mismatch"))
      (println label " " (- (current-jiffy) start) " " checksum))))
(measure 'preselected preselected)
(measure 'generic generic)
(measure 'cond-descriptor cond-descriptor)
(measure 'cond-map-field cond-map-field)
''')
    with tempfile.TemporaryDirectory(prefix='otium-mm-bench-') as directory:
        path = Path(directory) / 'bench.scm'
        path.write_text('\n'.join(forms))
        result = subprocess.run([binary, '--no-project', str(path)], text=True,
                                capture_output=True, timeout=180, check=True)
    measurements = {}
    checksums = set()
    for line in result.stdout.splitlines():
        strategy, elapsed, checksum = line.split()
        measurements.setdefault(strategy, []).append(int(elapsed))
        checksums.add(checksum)
    if len(checksums) != 1 or any(len(v) != 5 for v in measurements.values()):
        raise RuntimeError('incomplete or inconsistent measurements')
    for strategy, timings in measurements.items():
        writer.writerow([2 if binary_dispatch else 1, nx * ny, strategy, args.calls,
                         round(sorted(timings)[2] / args.calls, 2), next(iter(checksums))])
    sys.stdout.flush()
