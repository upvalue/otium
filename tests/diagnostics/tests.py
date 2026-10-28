"""Error codes, rendered error output, and the code catalog."""
import json
import re

from harness import ROOT, case, test

LIB = ['tests/diagnostics/lib']


case('diagnostic codes', 'tests/diagnostics/codes.ot', paths=LIB)

OUTPUTS = sorted((ROOT / 'tests/diagnostics/output').glob('*.ot'))


@test('diagnostic output')
def output(t):
    """Each program fails with exactly the stderr in its NAME.txt file.
    Set OTIUM_UPDATE_EXPECTED=1 to rewrite the files from the current output."""
    import os
    for program in OUTPUTS:
        expected_path = program.with_suffix('.txt')
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
    sources = [*ROOT.glob('c/src/*.c'), ROOT / 'lib/otium/expander.ot']
    used = set()
    for path in sources:
        if path.name.startswith('diagnostic-catalog'):
            continue
        used.update(re.findall(r'"(E\d{4})"', path.read_text()))
    t.check(not used - entries.keys(), 'uncatalogued codes:', sorted(used - entries.keys()))
    checks = (ROOT / 'tests/diagnostics/codes.ot').read_text()
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


@test('diagnostic json controls')
def json_controls(t):
    path = t.scratch / 'controls.ot'
    path.write_text('(require otium.diagnostics :as d) '
                             '(display (d/render-json {:message "a\\x1b;b\\x0;c" '
                             ':span {:path "p\\x1;q" :line 1 :column 1}}))')
    result = t.expect(t.otium(path))
    data = json.loads(result.stdout)
    t.check(data['message'] == 'a\x1bb\x00c', data)
    t.check(data['path'] == 'p\x01q', data)
