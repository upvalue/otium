"""Reduced-input correctness checks for the R7RS benchmark ports."""
import importlib.util
import os
from pathlib import Path
import shutil

from harness import BOTH, Skip, defaults, test

defaults(suite='bench', timeout=120)
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('r7rs_run', HERE / 'run.py')
RUNNER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUNNER)

INPUTS = {
    'fib': '1\n10\n55\n',
    'ack': '1\n2\n4\n11\n',
    'tak': '1\n10\n6\n3\n4\n',
    'cpstak': '1\n10\n6\n3\n4\n',
    'takl': '1\n(8 7 6 5 4 3 2 1)\n(4 3 2 1)\n(2 1)\n3\n',
    'nqueens': '1\n8\n92\n',
    'destruc': '1\n3\n2\n(() (1) (1) (1 ()) (1 ()) (1 ()) (1 ()) (1 ()) (1 ()) (1 1 () () ()))\n',
    'mperm': '1\n5\n2\n1\n0\n',
    'gcbench': '1\n8\n0\n',
    'quicksort': '1\n100\n1000\nignored\n',
    'fft': '1\n16\n0.0\n0.0\n',
    'equal': '4\n4\n4\n10\n10\n20\n#t\n',
    'deriv': None,
}


def vendor():
    if not RUNNER.vendor_available(HERE):
        raise Skip('vendored R7RS suite missing; see benchmarks/README.md')


def port(name):
    @test('r7rs ' + name, runtimes=BOTH)
    def run(t):
        vendor()
        text = INPUTS[name]
        if text is None:
            text = (HERE / 'vendor/inputs/deriv.input').read_text().replace('10000000', '1', 1)
        source = t.scratch / f'{name}.ot'
        source.write_text(RUNNER.otium_program(HERE, name, 'otium-test', text))
        flags = ['--c.max-depth=2000'] if t.runtime == 'c' else []
        t.expect(t.otium(*flags, source), contains=['+!CSVLINE!+otium-test,'], absent=['INCORRECT'])


for name in INPUTS:
    port(name)


@test('r7rs upstream guile')
def upstream(t):
    vendor()
    guile = shutil.which('guile')
    if not guile:
        raise Skip('guile not installed')
    source = t.scratch / 'fib-guile.scm'
    source.write_text(RUNNER.upstream_program(HERE, 'fib'))
    t.expect(t.run([guile, source], stdin=INPUTS['fib'], env={'GUILE_AUTO_COMPILE': '0'}),
             contains=['+!CSVLINE!+'], absent=['INCORRECT'])
