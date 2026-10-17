#!/usr/bin/env python3
"""Check module search, failures, retries, and reload with identical Otium source."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from otium_tools import runtime_argument, runtimes, command as runtime_command

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
runtime_argument(parser)
args = parser.parse_args()


with tempfile.TemporaryDirectory(prefix='otium-modules-') as scratch:
    directory = Path(scratch)
    first, second = directory/'first', directory/'second'
    first.mkdir()
    second.mkdir()
    (first/'choice.scm').write_text('(ns choice) (def value "first-directory")\n')
    (second/'choice.ot').write_text('(ns choice) (def value "second-directory")\n')
    (first/'suffix.ot').write_text('(ns suffix) (def value "ot")\n')
    (first/'suffix.scm').write_text('(error "wrong suffix")\n')
    (first/'retry.ot').write_text('''(ns retry)
(defonce attempts 0)
(set! attempts (+ attempts 1))
(when (= attempts 1) (error "first load fails"))
(defonce state #f)
(def (value) attempts)
''')
    (first/'cycle-a.ot').write_text('(ns cycle-a) (require cycle-b)\n')
    (first/'cycle-b.ot').write_text('(ns cycle-b) (require cycle-a)\n')
    (first/'mismatch.ot').write_text('(ns wrong) (def value 99)\n')
    source = directory/'checks.ot'
    source.write_text('''
(def (check expected actual)
  (unless (equal? expected actual) (error "module check failed" expected actual)))
(def (fails? form) (try (eval form) #f (catch (error? c) #t)))
(require choice suffix)
(check "first-directory" choice/value)
(check "ot" suffix/value)
(check #t (fails? '(require retry)))
(require retry :as r)
(check 2 r/attempts)
(check #f r/state)
(def (caller) (r/value))
(def saved r/value)
(set! r/value (fn () 42))
(check 42 (caller))
(check 2 (saved))
(require (retry :as r :reload))
(check 3 (caller))
(check #f r/state)
(check #t (fails? '(require mismatch)))
(check #t (fails? '(require mismatch)))
(check #t (fails? '(require cycle-a)))
(check #t (fails? '(require cycle-a)))
(require '(retry :as quoted :refer (value)))
(check 3 (value))
(check 3 (quoted/value))
(ns consumer (:require (retry :as r)))
(println (r/value))
''')
    for runtime in runtimes(args.runtime):
        env = dict(os.environ)
        if runtime == 'c':
            env['OTIUM_GC_STRESS'] = '1'
        result = subprocess.run(
            runtime_command(runtime, '--path', first, '--path', second, source),
            cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (runtime, result.stdout, result.stderr)
        assert result.stdout == '3\n', (runtime, result.stdout, result.stderr)
        print(f'{runtime}: module search, partial failure, retry, circular loads, namespace checks and reload passed')
