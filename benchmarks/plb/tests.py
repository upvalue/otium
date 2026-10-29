"""Check the Programming-Language-Benchmarks ports against upstream's outputs."""
import importlib.util
import json
from pathlib import Path

from harness import Failure, defaults, test

defaults(suite='bench', timeout=120)
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('plb_run', HERE / 'run.py')
RUNNER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUNNER)
MANIFEST = json.loads((HERE / 'manifest.json').read_text())


def port(name, entry):
    @test('plb ' + name)
    def run(t):
        if entry.get('fasta_input'):
            RUNNER.fasta_inputs(entry, 'test', t.scratch)
        for text, output in entry['tests']:
            arguments = ['-L', HERE / 'lib', HERE / 'ports' / f'{name}.ot', '--', *text.split()]
            code, _, stdout, stderr, _ = RUNNER.common.execute(
                t.command(*arguments), 60, cwd=t.scratch, env=t.environment(None), binary=True)
            if code != 0:
                raise Failure(f'{name} {text}: exit {code}\n{stderr}')
            if entry.get('md5_body'):
                stdout = RUNNER.md5_body(stdout)
            expected = (RUNNER.VENDOR / 'algorithm' / name / output).read_bytes()
            t.check(RUNNER.normalize(stdout) == RUNNER.normalize(expected),
                    f'{name} {text}: output differs from {output}:\n',
                    stdout.decode(errors='replace')[:2000])


for name, entry in MANIFEST['benchmarks'].items():
    port(name, entry)
