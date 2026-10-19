#!/usr/bin/env python3
"""Shared Otium reader fixtures, evaluation, loading, and streamed input."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from otium_tools import runtime_argument, runtimes, command as runtime_command

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = Path(__file__).with_name('reader')


def literal(text):
    return json.dumps(text, ensure_ascii=False).replace('\\u0000', '\\0')


def run(command, source, expected=0):
    result = subprocess.run(command, input=source, text=True, capture_output=True, cwd=ROOT, timeout=30)
    assert result.returncode == expected, (command, result.stdout, result.stderr)
    return result


parser = argparse.ArgumentParser(description=__doc__)
runtime_argument(parser)
args = parser.parse_args()

fixtures = json.loads((FIXTURES / 'datums.json').read_text())
with tempfile.TemporaryDirectory(prefix='otium-reader-') as directory:
    directory = Path(directory)
    for runtime in runtimes(args.runtime):
        command = runtime_command(runtime)
        path = directory / 'checks.ot'
        source = (FIXTURES / 'values.ot').read_text()
        for text, expected in fixtures['positive']:
            source += f'\n(check {expected} (read-string {literal(text)}))'
        for text in fixtures['negative']:
            source += f'\n(check #t (fails? (fn () (read-string {literal(text)}))))'
        if runtime == 'chez':
            # Token mode reads with the same reader, so it accepts and rejects the
            # same text. Empty input and trailing datums only matter to read-string.
            source += '\n(require otium.reader)'
            for text, expected in fixtures['positive']:
                source += f'\n(check #t (pair? (otium.reader/tokens {literal(text)})))'
            for text in fixtures['negative']:
                if text not in ('', '; only', '1 2'):
                    source += f'\n(check #t (fails? (fn () (otium.reader/tokens {literal(text)}))))'
            source += '''
(check #f (exists (fn (x) nil) '(1 2)))
(check nil (for-all (fn (x) nil) '(1 2)))
(check 7 (exists (fn (x) (if (= x 2) 7 nil)) '(1 2 3)))
(check 8 (for-all (fn (x) 8) '(1 2)))
(check 3 (let ((n 0)) (while (if (= n 3) nil #t) (set! n (+ n 1))) n))
(require chez)
(check 8 (chez/if (chez/void) 8 9))
(check '(:a nil) (chez/case 1 ((1) '(:a nil)) (else #f)))
(check '(:a nil) (chez/let-values (((x y) (chez/values ':a 'nil))) (list x y)))
(check nil ((chez/case-lambda (() 'nil))))
(check '(nil :a nil) `(nil :a ,nil))

'''
        source += '\n(println "reader fixtures passed")\n'
        path.write_text(source)
        result = run(command + [str(path)], '')
        assert result.stdout == 'reader values passed\nreader fixtures passed\n', result
        module = directory / 'readerfixture.ot'
        module.write_text('(ns readerfixture)\n(def value [nil :module 0xff])\n')
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
