#!/usr/bin/env python3
"""Run public core and FFI tests against the prototype, retaining their assertions."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
sys.dont_write_bytecode = True
import harness

DEFINITIONS = ['tests/tests.py', 'tests/library/tests.py', 'tests/modules/tests.py',
               'tests/reader/tests.py', 'tests/conditions/tests.py',
               'tests/syntax-quote/tests.py', 'tests/ffi/tests.py',
               'tests/resource/tests.py', 'tests/sqlite/tests.py',
               'tests/diagnostics/tests.py', 'tests/cli/tests.py']


class Context(harness.Context):
    def command(self, *arguments, runtime=None, project=False):
        if runtime not in (None, 'c'):
            raise harness.Skip('cross-runtime launcher test needs separate integration')
        return [str(ROOT / 'prototype/otium-proto'), *map(str, arguments)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    subprocess.run(['make', '-C', str(ROOT / 'prototype'), 'otium-proto',
                    'ffi_fixture.so', 'ffi_other.so'], check=True)
    original_library = harness.library
    fixtures = {'ffi-fixture-17': ROOT / 'prototype/ffi_fixture.so',
                'ffi-fixture-29': ROOT / 'prototype/ffi_other.so'}
    harness.library = lambda name: fixtures[name] if name in fixtures else original_library(name)
    for index, name in enumerate(DEFINITIONS):
        path = ROOT / name
        harness._defaults.clear()
        harness._defaults['source'] = path
        spec = importlib.util.spec_from_file_location(f'public_core_{index}', path)
        spec.loader.exec_module(importlib.util.module_from_spec(spec))
    results = []
    for test in harness.TESTS.values():
        if test.runtime != 'c':
            continue
        context = Context('c')
        status, detail = 'pass', ''
        try:
            test.function(context)
        except harness.Skip as error:
            status, detail = 'skip', str(error)
        except Exception as error:
            status, detail = 'fail', str(error)
        finally:
            context.close()
        results.append(dict(name=test.name, source=str(test.source.relative_to(ROOT)),
                            status=status, detail=detail))
        print(f'{status}: {test.name}')
    report = dict(definitions=DEFINITIONS, results=results,
                  counts={key: sum(r['status'] == key for r in results)
                          for key in ('pass', 'fail', 'skip')})
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['counts']))
    return int(any(r['status'] != 'pass' for r in results))


if __name__ == '__main__':
    sys.exit(main())
