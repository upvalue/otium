#!/usr/bin/env python3
"""Exercise preload ordering and incremental input through the actual CLI."""
from pathlib import Path
import os
import subprocess
import tempfile

CLI = Path(os.environ.get('OTIUM_TEST_CLI', Path(__file__).resolve().with_name('otium-proto'))).resolve()


def run(args=(), source='', status=0, output=None):
    result = subprocess.run([str(CLI), *map(str, args)], input=source,
                            text=True, capture_output=True, timeout=30)
    assert result.returncode == status, result
    if output is not None:
        assert result.stdout == output, result
    return result


run(['--repl'], 'nil\n(println\n [nil :repl])\n', output='nil\n[nil :repl]\nnil\n')
run(source='(+ 1 2) (+ 3 4)\n', output='3\n7\n')
run(['--repl'], '(def n 0) (begin\n (set! n (+ n 1))\n n)\nn\n', output='0\n1\n1\n')
run(['--repl'], "'\n(a .\n b\n)\n", output='(a . b)\n')
run(['--repl'], '(str "a\nb")\n', output='a\nb\n')
run(['--repl'], '; comment only\n\n', output='')
run(['--repl'], '(+ 1\n', status=1)
run(['--repl'], '"unfinished\n', status=1)
r = run(['--repl'], ')\n(/ 1 0)\n42\n', output='42\n')
assert 'unexpected closing' in r.stderr and 'zero' in r.stderr, r
run(['--repl'], '(def n 0)\n(set! n 1) (+\n 2 3)\nn\n', output='0\nnil\n5\n1\n')
r = run(['--repl'], "'(a . b c)\n42\n", output='42\n')
assert 'expected closing delimiter' in r.stderr, r
run(['-e', '(exit)'], output='')
run(['--repl'], '(exit 7)\n(println :wrong)\n', status=7, output='')
run(['--load'], status=2)
run(['--native', '-e', '1'], status=2)
with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    first, second, script = [root / name for name in ['first.ot', 'second.ot', 'script.ot']]
    first.write_text('(def order [:first]) (def answer 40)')
    second.write_text('(push! order :second) (set! answer (+ answer 2))')
    script.write_text('(println order answer)')
    run(['--load', first, '--load', second, script], output='[:first :second] 42\n')
    run(['--load', first, '--load', second, '-e', 'answer'], output='42\n')
    run(['--load', first, '--repl', script], 'answer\n', output='[:first] 40\n40\n')
    first.write_text('(error "preload failure")')
    r = run(['--load', first, script], status=1, output='')
    assert str(first) in r.stderr and 'preload failure' in r.stderr, r
    run(['--load', root / 'missing', script], status=1)
    first.write_text('(exit 7)')
    run(['--load', first, script], status=7, output='')
    first.write_text('')
    run(['--load', first, '-e', '42'], output='42\n')
    first.write_text(' ' * 4096)
    run(['--load', first, '-e', '42'], output='42\n')
fixture = Path(__file__).resolve().parent.parent / 'tests/cli/arguments.ot'
args = ['--help', '', 'two words', 'λ', '--', '-file', '--c.heap-max=1']
expected = ''.join(f'{len(arg)}:{arg}\n' for arg in args)
run([fixture, '--', *args], output=expected + str(fixture) + '\n')
run(['--', fixture, *args], output=expected + str(fixture) + '\n')
run(['-e', '(otium.process/arguments)', '--', 'λ', ''], output='("λ" "")\n')
run(['-e', '(otium.process/script-name)'], output='nil\n')
with tempfile.TemporaryDirectory() as temp:
    preload = Path(temp) / 'preload.ot'
    preload.write_text('(println (otium.process/arguments))')
    run(['--load', preload, fixture, '--', *args],
        output='("--help" "" "two words" "λ" "--" "-file" "--c.heap-max=1")\n' + expected + str(fixture) + '\n')
print('CLI preload, script arguments and incremental REPL tests passed')

large = '(bytevector-length (make-bytevector 2097152))'
run(['-e', large], output='2097152\n')
run(['--heap=1048576', '-e', large], status=1)
run(['--heap-max=1048576', '-e', large], status=1)
run(['--heap=1048576', '--heap-max=4194304', '-e', large], output='2097152\n')
run(['--heap=2097152', '--heap-max=1048576', '-e', '1'], status=2)
r = run(['--stats', '-e', '42'], output='42\n')
assert 'heap-capacity=1048576' in r.stderr, r
print('CLI bounded growth and explicit fixed heaps passed')
