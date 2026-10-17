#!/usr/bin/env python3
"""Exercise portable foreign calls using libm and an independent ABI fixture."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime', choices=('c', 'chez', 'both'), default='both')
parser.add_argument('--stress', action='store_true')
args = parser.parse_args()
runtimes = ('c', 'chez') if args.runtime == 'both' else (args.runtime,)
mac = platform.system() == 'Darwin'
extension = 'dylib' if mac else 'so'
helper = ROOT / 'shared/build' / (platform.machine() + '-' + platform.system().lower()) / ('libotium-ffi.' + extension)
math_names = '("/usr/lib/libSystem.B.dylib")' if mac else '("libm.so.6" "libm.so")'


def run(runtime, source, directory, expected):
    script = directory / (runtime + '.scm')
    prefix = "(require '(otium.ffi :as ffi))\n" if runtime == 'c' else '(require otium.ffi :as ffi)\n'
    collect = '(define (churn n) (if (> n 0) (begin (list n n n) (churn (- n 1))) #f))\n(define (test-collect) (churn 2000))\n' if runtime == 'c' else '(define (test-collect) (collect))\n'
    script.write_text(prefix + collect + '(define wide-integers? ' + ('#f' if runtime == 'c' else '#t') + ')\n' + source)
    command = [str(ROOT / 'c/otium'), '--no-project'] if runtime == 'c' else [os.environ.get('CHEZ', 'chez'), '--libdirs', str(ROOT / 'chez'), '--script', str(ROOT / 'chez/main.ss')]
    if runtime == 'c' and args.stress:
        command.append('--c.gc-force-compact')
    env = dict(os.environ, OTIUM_FFI_LIBRARY=str(helper))
    if runtime == 'c' and args.stress:
        env['OTIUM_GC_STRESS'] = '1'
    result = subprocess.run(command + [str(script)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (runtime, result.stdout, result.stderr)
    assert result.stdout == expected, (runtime, result.stdout, result.stderr, expected)


with tempfile.TemporaryDirectory(prefix='otium-ffi-tests-') as temp:
    directory = Path(temp)
    fixture = directory / ('fixture.' + extension)
    other = directory / ('other.' + extension)
    for path, identity in ((fixture, 17), (other, 29)):
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-fPIC', '-dynamiclib' if mac else '-shared', '-DFIXTURE_ID=' + str(identity), str(ROOT / 'shared/tests/ffi/fixture.c'), '-o', str(path)], check=True)
    definitions = f'(define fixture-path "{fixture}")\n(define other-path "{other}")\n'
    common = (ROOT / 'shared/tests/ffi/common.scm').read_text()
    for runtime in runtimes:
        run(runtime, '(define math-library-names ' + "'" + math_names + ')\n' + (ROOT / 'examples/shared/math.scm').read_text(), directory, '1.0\n5.0\n0.75\n12.0\n')
        run(runtime, definitions + common, directory, 'FFI checks: 197\n')
        print(runtime + ': libm and fixture checks passed' + (' under GC stress' if runtime == 'c' and args.stress else ''))
