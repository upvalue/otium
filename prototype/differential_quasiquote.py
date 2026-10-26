#!/usr/bin/env python3
"""Quasiquote comparisons against the primary runtime, including nested depth."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CASES = [
    "(println `(a ,(+ 1 2) ,@'(4 5) . tail))",
    "(println (let ((x 42)) `(a `(b ,x ,,x))))",
    "(println (let ((x 42)) `(a . ,x)))",
    "(println (let ((x 42)) `(a unquote x)))",
    "(println `(a . ,@'(b c)))",
    "(println `(,@'(1 2) . 3))",
    "(println `(,@nil))",
    "(println `(,@[1 2]))",
    "(println `(,@'(1 . 2)))",
    "(println (let ((x 42)) (quasiquote (unquote  x ignored))))",
    "(println (quasiquote (quasiquote (unquote x ignored))))",
    "(println (let ((x '(1 2))) `(a `(b ,@x))))",
    "(println (let ((x 42)) ``(,,x)))",
    "(println (let ((x 42)) `(unquote-splicing x)))",
    "(println (list `[1 ,(+ 1 2)] `{:a ,(+ 20 22)}))",
    "(def (f) `(a b)) (println (eq? (f) (f)))",
    "(def (f x) `(,x a b)) (println (eq? (cdr (f 1)) (cdr (f 2))))",
    "(def (f x) `(a b ,x)) (println (eq? (cdr (f 1)) (cdr (f 2))))",
    "(def (f) `(,@'(1 2) 3)) (println (eq? (f) (f)))",
    "(println (let ((x '(1 . 2))) (eq? (cdr `(a ,@x)) x)))",
    "(println (let ((x '(1 2))) `(,@x ,(begin (set-car! x 9) 3))))",
    "(println (let ((n 0)) `(,(begin (set! n (+ n 1)) n) ,(begin (set! n (+ n 1)) n))))",
    "(def cons (fn (a b) 99)) (def append (fn (a b) 99)) (println `(a ,@'(b c) ,42))",
    "(def (loop n) (if (= n 0) 42 `(,@(loop (- n 1))))) (println (loop 10000))",
]
# Combine static, evaluated, spliced and nested pieces without assuming their results.
parts = ['a', ',x', ",@'(2 3)", '(a ,x)', '`(b ,x ,,x)', '()', ':tag']
for i, left in enumerate(parts):
    right = parts[(i * 3 + 1) % len(parts)]
    CASES.append(f'(println (let ((x 42)) `({left} {right} . ,x)))')
    CASES.append(f'(println (let ((x 42)) `({left} {right})))')
INVALID = [',1', ',@1', '(quasiquote)', '(quasiquote 1 2)', '`(unquote)',
           '`(unquote-splicing)', '`(,@nil 1)', '`(,@[1 2] 3)', "`(,@'(1 . 2) 3)",
           '`,@(unquote 1)', '`(,(def x 1))']

def main():
    commands = [[str(ROOT / 'prototype/otium-proto')],
                [str(ROOT / 'otium'), '--runtime=chez', '--no-project']]
    with tempfile.TemporaryDirectory(prefix='otium-qq-differential-') as directory:
        entry = pathlib.Path(directory) / 'entry.ot'
        for source in CASES + INVALID:
            entry.write_text(source)
            results = [subprocess.run([*command, str(entry)], cwd=ROOT,
                       capture_output=True, text=True, timeout=30) for command in commands]
            if source in INVALID:
                assert all(r.returncode for r in results), (source, [(r.returncode, r.stdout, r.stderr) for r in results])
            else:
                assert all(not r.returncode for r in results), (source, [r.stderr for r in results])
                assert all(r.stdout == results[0].stdout for r in results), (source, [r.stdout for r in results])
    print(f'Quasiquote differential checks passed: {len(CASES)} programs, {len(INVALID)} errors')

if __name__ == '__main__':
    main()
