#!/usr/bin/env python3
"""Compare the implemented FFI subset with the primary Chez implementation."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / 'prototype/build/release/ffi_fixture.so'
CASES = [
    '(println (apply (ffi/bind lib "echo_int" \'(int) \'int #t) \'(42)))',
    '(println (apply (ffi/bind lib "echo_utf8" \'(utf8) \'utf8 #t) ["héllo"]))',

    '(println ((ffi/bind lib "fixture_id" \'() \'int #t)))',
    '(def echo (ffi/bind lib "echo_i8" \'(i8) \'i8 #f)) (println (echo -128)) (println (echo 127))',
    '(def echo (ffi/bind lib "echo_u32" \'(u32) \'u32 #t)) (println (echo 4294967295))',
    '(def echo (ffi/bind lib "echo_bool" \'(bool) \'bool #t)) (println (echo #t)) (println (echo #f))',
    '(println (= ((ffi/bind lib "echo_float" \'(float) \'float #t) 16777217) 16777216.0))',
    '(def echo (ffi/bind lib "echo_utf8" \'(utf8) \'utf8 #t)) (println (echo "héllo 🌍")) '
    '(println (ffi/null? (echo ffi/null)))',
    '(def saved ((ffi/bind lib "changing_text" \'() \'utf8 #t))) '
    '(println ((ffi/bind lib "change_text" \'() \'void #t))) (println saved) '
    '(println ((ffi/bind lib "changing_text" \'() \'utf8 #t)))',
    '(def p (ffi/alloc \'i32 4)) (println (ffi/set! p \'i32 2 42)) '
    '(println (ffi/ref p \'i32 2)) (println (ffi/allocation-count)) '
    '(println (ffi/allocation-bytes)) (println (ffi/free! p)) (println (ffi/free! p))',
    '(def p (ffi/alloc \'pointer 1)) '
    '((ffi/bind lib "pointer_out" \'(pointer pointer) \'void #t) p p) '
    '(println (ffi/pointer=? p (ffi/ref p \'pointer 0))) (ffi/free! p)',
    '(def p (ffi/alloc-utf8 "retained λ")) '
    '((ffi/bind lib "retain" \'(pointer) \'void #t) p) '
    '(println ((ffi/bind lib "retained_text" \'() \'utf8 #t))) '
    '(println (ffi/read-utf8 p 12)) (ffi/free! p)',
]
INVALID = [
    '(ffi/bind lib "fixture_id" \'() \'int 0)',
    '(ffi/bind lib "absent" \'() \'int #t)',
    '((ffi/bind lib "echo_u8" \'(u8) \'u8 #t) 256)',
    '((ffi/bind lib "echo_bool" \'(bool) \'bool #t) 1)',
    '((ffi/bind lib "bad_utf8" \'() \'utf8 #t))',
    '(def p (ffi/alloc \'u8 1)) (def v (ffi/view p 0 1)) (ffi/free! p) (ffi/ref v \'u8 0)',
]

for expression in [
    "(ffi/ref ffi/null 'u8 -1)",
    "(ffi/alloc 'u8 -1)",
    "(ffi/sizeof 'void)",
    "(ffi/field-ref ffi/null (ffi/define-struct! 'R '((x u8))) '(missing))",
    "(ffi/offset-of (ffi/define-struct! 'R '((x u8))) '(missing))",
]:
    CASES.append("(require otium.ffi :as ffi) "
                 "(try " + expression + " (catch (ffi/error? c) "
                 "(println (list (ffi/error-operation c) (ffi/error-reason c) "
                 "(ffi/error-argument c) (ffi/error-expected-type c)))))")



def main():
    commands = [[ROOT / 'prototype/otium-proto'],
                [ROOT / 'otium', '--runtime=chez', '--no-project']]
    with tempfile.TemporaryDirectory(prefix='otium-ffi-differential-') as directory:
        path = pathlib.Path(directory) / 'case.ot'
        for source in CASES + INVALID:
            results = []
            for command in commands:
                prefix = '(require otium.ffi :as ffi)\n' if '--runtime=chez' in command else ''
                path.write_text(prefix + f'(def lib (ffi/open-library \'("{FIXTURE}")))\n' + source + '\n')
                results.append(subprocess.run([*map(str, command), str(path)], cwd=ROOT,
                                              capture_output=True, text=True, timeout=60))
            if source in INVALID:
                assert all(r.returncode != 0 for r in results), (source, results)
            else:
                assert all(r.returncode == 0 for r in results), (source, [r.stderr for r in results])
                assert all(r.stdout == results[0].stdout for r in results), (source, [r.stdout for r in results])
    print(f'FFI differential checks passed: {len(CASES)} programs, {len(INVALID)} errors')


if __name__ == '__main__':
    main()
