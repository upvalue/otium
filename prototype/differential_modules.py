#!/usr/bin/env python3
"""Compare module behavior with public fixtures and the primary Chez runtime."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CASES = ['(require nested.worker :as worker) (println worker/value)', '(require) (println 42)', '(require api :as api) (println (api/read))',
    '(require choice suffix) (println (list (equal? choice/value "first-directory") (equal? suffix/value "ot")))',
    '(require api :as a :refer (read)) (println (list (a/read) (read)))',
    '(require api :refer (read)) (def (caller) (read)) (set! api/value 42) (println (caller))',
    '(require counter) (require counter) (println counter/n) (require counter :reload) (println counter/n)',
    '(ns consumer (:require (api :as a :refer (read)))) (println (list (a/read) (read)))',
    # Chez accepts this qualified value but rejects it directly in call-head position.
    '(require facade) (def forwarded facade/read) (println (forwarded))',
    "(require '(api :as a :refer (read))) (println (list (a/read) (read)))",
    "(require 'suffix) (println (equal? suffix/value \"ot\"))",
    '(require suffix :as s :refer (value)) (def (old) value) (require choice :as s :refer (value)) (println (list (equal? s/value "first-directory") (equal? value "first-directory") (equal? (old) "ot")))',
    '(require plain) (println plain/value)',
    '(println (list (when #t 1 42) (when nil 9) (unless #f 43) (when #t)))',
    '(def (loop n) (when (> n 0) (loop (- n 1)))) (println (loop 10000))',
]
INVALID = ['(require facade :refer (read))', '(require mismatch)', '(require cycle-a)', '(require absent)',
    '(require api :refer (secret))', '(require api :refer (absent))',
    '(ns other) (require api :as other)', '(require api :as)', '(require api :refer x)',
    '(require api :refer (read)) (def read 99)',
    '(def read 99) (require api :refer (read))',
    '(require api :unknown)', '(require api . tail)', '(fn () (require api))',
    '(when)', '(unless)', '(when #t (def x 1))', "(require 'suffix :as s)"]

def main():
    with tempfile.TemporaryDirectory(prefix='otium-module-differential-') as directory:
        folder = pathlib.Path(directory)
        (folder / 'api.ot').write_text('(ns api) (def value 41) (def- secret 7) (def (read) value)')
        (folder / 'counter.ot').write_text('(ns counter) (defonce n 0) (set! n (+ n 1))')
        (folder / 'facade.ot').write_text('(ns facade) (require api :refer (read))')
        (folder / 'plain.ot').write_text('(def value 42)')
        (folder / 'nested').mkdir()
        (folder / 'nested/worker.ot').write_text('(ns nested.worker) (def value 42)')
        paths = ['-L', str(ROOT / 'tests/modules/first'), '-L', str(ROOT / 'tests/modules/second'), '-L', directory]
        commands = [[str(ROOT / 'prototype/otium-proto')],
                    [str(ROOT / 'otium'), '--runtime=chez', '--no-project']]
        entry = folder / 'entry.ot'
        for source in CASES + INVALID:
            entry.write_text(source)
            results = [subprocess.run([*command, *paths, str(entry)], cwd=ROOT,
                       capture_output=True, text=True, timeout=30) for command in commands]
            if source in INVALID:
                assert all(r.returncode for r in results), (source, [(r.returncode, r.stdout, r.stderr) for r in results])
            else:
                assert all(not r.returncode for r in results), (source, [r.stderr for r in results])
                assert all(r.stdout == results[0].stdout for r in results), (source, [r.stdout for r in results])
    print(f'Module differential checks passed: {len(CASES)} programs, {len(INVALID)} errors')

if __name__ == '__main__':
    main()
