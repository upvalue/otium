"""Error codes on both runtimes, rendered error output, and the code catalog."""
import json
import re

from harness import BOTH, ROOT, case, test

LIB = ['tests/diagnostics/lib']

case('diagnostic codes', 'tests/diagnostics/codes.ot', paths=LIB)
case('diagnostic codes chez', 'tests/diagnostics/codes-chez.ot', runtimes=['chez'])

OUTPUTS = sorted((ROOT / 'tests/diagnostics/output').glob('*.ot'))


@test('diagnostic output', runtimes=BOTH)
def output(t):
    """Each program fails with exactly the stderr in its NAME.RUNTIME.txt file.
    Set OTIUM_UPDATE_EXPECTED=1 to rewrite the files from the current output."""
    import os
    for program in OUTPUTS:
        expected_path = program.with_suffix(f'.{t.runtime}.txt')
        result = t.run(t.command('-L', *LIB, program.relative_to(ROOT)))
        t.check(result.status == 1, program.name, 'exit status', result)
        if os.environ.get('OTIUM_UPDATE_EXPECTED'):
            expected_path.write_text(result.stderr)
            continue
        expected = expected_path.read_text()
        t.check(result.stderr == expected, f'{program.name} stderr differs',
                f'--- expected\n{expected}--- actual\n{result.stderr}')


@test('diagnostic catalog')
def catalog(t):
    """Codes in the sources are catalogued, and each catalogued code is tested."""
    entries = json.loads((ROOT / 'lib/catalog/diagnostics.json').read_text())
    sources = [*ROOT.glob('chez/otium/*.sls'), ROOT / 'c/src/otium.c', ROOT / 'c/src/main.c',
               ROOT / 'lib/otium/expander.ot']
    used = set()
    for path in sources:
        if path.name.startswith('diagnostic-catalog'):
            continue
        used.update(re.findall(r'"(E\d{4})"', path.read_text()))
    t.check(not used - entries.keys(), 'uncatalogued codes:', sorted(used - entries.keys()))
    checks = ''.join(p.read_text() for p in ROOT.glob('tests/diagnostics/codes*.ot'))
    untested = [code for code in entries if code.startswith('E') and f'"{code}"' not in checks]
    t.check(not untested, 'error codes without a check in tests/diagnostics:', untested)
    fixtures = ''.join(p.read_text() for p in ROOT.glob('tests/lint/fixtures/*.ot'))
    untested = [code for code in entries if code.startswith('L') and code not in fixtures]
    t.check(not untested, 'lint codes without a fixture in tests/lint/fixtures:', untested)
    for code, entry in entries.items():
        t.check(entry.get('title') and entry.get('message') and entry.get('explanation'),
                code, 'needs a title, message and explanation')
        if code.startswith('L'):
            t.check(entry.get('level') in ('deny', 'warn', 'allow'), code, 'has no valid level')
