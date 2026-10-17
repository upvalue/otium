#!/usr/bin/env python3
"""Shared Otium reader fixtures, evaluation, loading, and streamed input."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).with_name('reader')


def literal(text):
    return json.dumps(text, ensure_ascii=False).replace('\\u0000', '\\0')


def run(command, source, expected=0):
    result = subprocess.run(command, input=source, text=True, capture_output=True, cwd=ROOT, timeout=30)
    assert result.returncode == expected, (command, result.stdout, result.stderr)
    return result


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--stress', action='store_true', help='Collect at every C allocation')
args = parser.parse_args()
if args.stress:
    os.environ['OTIUM_GC_STRESS'] = '1'

fixtures = json.loads((FIXTURES / 'datums.json').read_text())
with tempfile.TemporaryDirectory(prefix='otium-reader-') as directory:
    directory = Path(directory)
    for runtime, executable in [('c', ROOT / 'c/otium'), ('chez', ROOT / 'chez/run')]:
        command = [str(executable), '--no-project']
        path = directory / 'checks.scm'
        source = (FIXTURES / 'values.scm').read_text()
        for text, expected in fixtures['positive']:
            source += f'\n(check {expected} (read-string {literal(text)}))'
        for text in fixtures['negative']:
            source += f'\n(check #t (fails? (lambda () (read-string {literal(text)}))))'
        if runtime == 'chez':
            source += '''
(check #f (exists (lambda (x) nil) '(1 2)))
(check nil (for-all (lambda (x) nil) '(1 2)))
(check 7 (exists (lambda (x) (if (= x 2) 7 nil)) '(1 2 3)))
(check 8 (for-all (lambda (x) 8) '(1 2)))
(check 3 (do ((n 0 (+ n 1))) ((if (= n 3) #t nil) n)))
(check 5 (cond (nil 99) (5 => (lambda (x) x))))
(check 8 (chez/if (chez/void) 8 9))
(check '(:a nil) (case 1 ((1) (list :a nil)) (else #f)))
(check '(:a nil) (let-values (((x y) (chez/values :a nil))) (list x y)))
(check nil ((case-lambda (() nil))))
(check '(nil :a nil) `(nil :a ,nil))

'''
        source += '\n(println "reader fixtures passed")\n'
        path.write_text(source)
        result = run(command + [str(path)], '')
        assert result.stdout == 'reader values passed\nreader fixtures passed\n', result
        module = directory / 'readerfixture.scm'
        module.write_text('(ns readerfixture)\n(define value [nil :module 0xff])\n')
        require = "(require '(readerfixture :as r))" if runtime == 'c' else '(require readerfixture :as r)'
        path.write_text(require + '\n(println (get r/value 1) (if (get r/value 0) 99 7))\n')
        result = run(command + ['--path', str(directory), str(path)], '')
        assert result.stdout == ':module 7\n', result
        result = run(command + ['--repl'], "nil\n(println\n [nil :repl])\n(println (read-string \"'[]\"))\n")
        assert 'nil\n' in result.stdout and '[nil :repl]' in result.stdout and '(quote (array))' in result.stdout, result
        for text in ['#(1)', '(1]', '"\\q"', '#!/bin/otium\n1']:
            path.write_text(text)
            result = run(command + [str(path)], '', 1)
            assert str(path) in result.stderr, result
        print(f'{runtime}: {len(fixtures["positive"])} positive, {len(fixtures["negative"])} negative reader fixtures; values, module, file and REPL checks passed')
