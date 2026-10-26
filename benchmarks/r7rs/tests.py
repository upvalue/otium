"""Reduced-input correctness checks for the R7RS benchmark ports."""
import hashlib
import importlib.util
import json
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
    'browse': None,
    'nucleic': None,
    'matrix': '2\n3\n3\n(((1 1 1) (1 1 -1) (1 -1 1) (1 -1 -1)))\n',
    'graphs': '2\n3\n5\n',
    'peval': None,
    'earley': '2\n6\n42\n',
    'sboyer': '2\n1\n591777\n',
    'nboyer': '2\n1\n591777\n',
    'conform': None,
    'simplex': '2\n740.0\n(#(4 1 3 2) #(0 5 7 6))\n\n',
    'ray': '2\n1\n"outputs/ray.output"\nok\n',
    'lattice': '2\n33\n10\n',
    'paraffins': '2\n8\n18\n',
    'mazefun': '2\n5\n5\n((_ * _ * _) (_ * _ * _) (_ * _ _ _) (_ * _ * *) (_ _ _ _ _))\n',
    'bv2string': '2\n20\n100\n0\n',
    'read1': '2\n"inputs/read1.txt"\n(should return this list)\n',
    'sum1': '2\n"inputs/sum1.txt"\n6.75\n',
    'wc': '2\n"inputs/wc.txt"\n(2 5 28)\n',
    'tail': '2\n"inputs/tail.txt"\n"outputs/tail.output"\nignored\n',
    'cat': '2\n"inputs/cat.txt"\n"outputs/cat.output"\nignored\n',
    'triangl': '2\n22\n1\n(22 34 31 15 7 1 20 17 25 6 5 13 32)\n',
    'pnpoly': '2\n#(0. 1. 1. 0. 0. 1. -.5 -1. -1. -2. -2.5 -2. -1.5 -.5 1. 1. 0. -.5 -1. -.5)\n#(0. 0. 1. 1. 2. 3. 2. 3. 0. -.5 -1.  -1.5 -2. -2. -1.5 -1. -.5 -1. -1. -.5)\n6\n',
    'string': '2\n100\n118\n',
    'mbrot': '2\n10\n5\n',
    'primes': '2\n30\n(2 3 5 7 11 13 17 19 23 29)\n',
    'array1': '3\n100\n100\n',
    'ntakl': '2\n(8 7 6 5 4 3 2 1)\n(4 3 2 1)\n(2 1)\n3\n',
    'divrec': '2\n20\n10\n',
    'diviter': '2\n20\n10\n',
    'fibfp': '2\n10.0\n55.0\n',
    'sumfp': '2\n100.0\n5050.0\n',
    'sum': '2\n100\n5050\n',
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


CANONICAL_COUNTS = {'browse': 2, 'deriv': 1, 'conform': 2, 'peval': 2, 'nucleic': 2}

FIXTURES = json.loads((HERE / "test-fixtures.json").read_text())


def vendor():
    if not RUNNER.vendor_available(HERE):
        raise Skip('vendored R7RS suite missing; see benchmarks/README.md')


def port(name):
    @test('r7rs ' + name, runtimes=BOTH)
    def run(t):
        vendor()
        text = INPUTS[name]
        if text is None:
            canonical = (HERE / 'vendor/inputs' / f'{name}.input').read_text()
            text = str(CANONICAL_COUNTS[name]) + '\n' + canonical.split('\n', 1)[1]
        fixture = FIXTURES.get(name, {})
        for directory in fixture.get('fixture_directories', []):
            (t.scratch / directory).mkdir(parents=True, exist_ok=True)
        for path, contents in fixture.get('fixtures', {}).items():
            target = t.scratch / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(contents)
        source = t.scratch / f'{name}.ot'
        source.write_text(RUNNER.otium_program(HERE, name, 'otium-test', text))
        flags = ['--c.max-depth=2000'] if t.runtime == 'c' else []
        t.expect(t.otium(*flags, source, cwd=t.scratch), contains=['+!CSVLINE!+otium-test,'], absent=['INCORRECT'])
        for path, expected in fixture.get('expected_outputs', {}).items():
            t.check((t.scratch / path).read_text() == expected, 'unexpected contents:', path)
        for path, expected in fixture.get('expected_output_hashes', {}).items():
            actual = hashlib.sha256((t.scratch / path).read_bytes()).hexdigest()
            t.check(actual == expected, 'unexpected contents:', path)


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
