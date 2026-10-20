#!/usr/bin/env python3
"""Exercise portable foreign calls using libm and an independent ABI fixture."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile

from otium_tools import command as runtime_command, environment

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime', choices=('c', 'chez', 'both'), default='both')
args = parser.parse_args()
runtimes = ('c', 'chez') if args.runtime == 'both' else (args.runtime,)
mac = platform.system() == 'Darwin'
extension = 'dylib' if mac else 'so'
math_names = '("/usr/lib/libSystem.B.dylib")' if mac else '("libm.so.6" "libm.so")'


def run(runtime, source, directory, expected, options=(), status=0, error=None):
    script = directory / (runtime + '.ot')
    prefix = "(require '(otium.ffi :as ffi))\n" if runtime == 'c' else '(require otium.ffi :as ffi)\n'
    collect = '(def (churn n) (if (> n 0) (begin (list n n n) (churn (- n 1))) #f))\n(def (test-collect) (churn 2000))\n' if runtime == 'c' else '(require chez)\n(def (test-collect) (chez/collect))\n'
    script.write_text(prefix + collect + '(def wide-integers? ' + ('#f' if runtime == 'c' else '#t') + ')\n' + source)
    command = runtime_command(runtime)
    if runtime == 'c' and os.environ.get('OTIUM_GC_STRESS'):
        command.append('--c.gc-force-compact')
    env = environment()
    result = subprocess.run(command + list(options) + [str(script)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
    if status is None:
        assert result.returncode != 0, (runtime, result.stdout, result.stderr)
    else:
        assert result.returncode == status, (runtime, result.stdout, result.stderr)
    assert result.stdout == expected, (runtime, result.stdout, result.stderr, expected)
    assert error is None or error in result.stderr, (runtime, result.stderr, error)


with tempfile.TemporaryDirectory(prefix='otium-ffi-tests-') as temp:
    directory = Path(temp)
    fixture = directory / ('fixture.' + extension)
    other = directory / ('other.' + extension)
    for path, identity in ((fixture, 17), (other, 29)):
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-fPIC', '-dynamiclib' if mac else '-shared', '-DFIXTURE_ID=' + str(identity), str(ROOT / 'tests/ffi/fixture.c'), '-o', str(path)], check=True)
    definitions = f'(def fixture-path "{fixture}")\n(def other-path "{other}")\n'
    common = (ROOT / 'tests/ffi/common.ot').read_text()
    callbacks = (ROOT / 'tests/ffi/callbacks.ot').read_text()
    library = '(def lib (ffi/open-library (list fixture-path)))\n(def call-n (ffi/bind lib "call_n" \'(callback-pointer int) \'int))\n'
    # An exit inside a callback is repeated once the foreign call returns.
    exit_check = library + '(call-n (ffi/callback \'(int) \'int (fn (x) (display "before") (newline) (exit 3))) 1)\n(display "after")\n'
    thread_check = library + '((ffi/bind lib "call_on_thread" \'(callback-pointer) \'void) (ffi/callback \'() \'void (fn () nil)))\n'
    for runtime in runtimes:
        run(runtime, '(def math-library-names ' + "'" + math_names + ')\n' + (ROOT / 'examples/math.ot').read_text(), directory, '1.0\n5.0\n0.75\n12.0\n')
        run(runtime, definitions + common, directory, 'FFI checks: 203\n')
        modules = directory / 'modules'
        modules.mkdir(exist_ok=True)
        run(runtime, definitions + f'(def module-directory "{modules}")\n' + callbacks, directory, 'FFI callback checks: 42\n', ('-L', str(modules)))
        run(runtime, definitions + exit_check, directory, 'before\n', status=3)
        run(runtime, definitions + thread_check, directory, '', status=None, error='thread other than the Otium thread')
        print(runtime + ': libm and fixture checks passed' + (' under GC stress' if runtime == 'c' and os.environ.get('OTIUM_GC_STRESS') else ''))
