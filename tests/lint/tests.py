"""Lint fixtures, library behavior, and command-line options."""
import json
import re
from harness import BOTH, ROOT, case, test

case('lint checks', 'tests/lint/checks.ot', runtimes=BOTH)

@test('lint fixtures', runtimes=BOTH)
def fixtures(t):
    for path in sorted((ROOT / 'tests/lint/fixtures').glob('*.ot')):
        expected = sorted(code for line in path.read_text().splitlines()
                          if ';~' in line for code in re.findall(r'L\d{4}', line.split(';~', 1)[1]))
        result = t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--format=json', path])
        actual = [json.loads(line) for line in result.stdout.splitlines()]
        t.check(sorted(d['code'] for d in actual) == expected, path.name, result)
        t.check(result.status == int(any(d['severity'] == 'error' for d in actual)), result)
        for d in actual:
            t.check(d['path'] == str(path) and d['line'] > 0 and d['column'] > 0, d)

@test('lint cli', runtimes=BOTH)
def cli(t):
    path = t.scratch / 'example.ot'
    path.write_text('(if #t 1 (if #f 2 3))\n')
    for options, status in [((), 0), (('--deny-warnings',), 1),
                            (('--deny', 'if-chain'), 1), (('--allow', 'if-chain'), 0)]:
        t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--format=short', *options, path]), status=status)
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--allow', 'unknown', path]), status=2)
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--format=bad', path]), status=2)
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--rules']), contains=['L0001 if-chain'])
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'explain', 'L0001']), contains=['if-chain'])
    (t.scratch / 'project.ot').write_text('(lint :allow (if-chain))\n')
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--deny-warnings', path]), stdout='')
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--deny', 'if-chain', path]), status=1)
    (t.scratch / 'project.ot').write_text('(lint :allow (if-chains))\n')
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', path]), status=2)
    (t.scratch / 'project.ot').write_text('(lint :oops (if-chain))\n')
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', path]), status=2)
    (t.scratch / 'project.ot').unlink()
    path.write_text('(def (f) (def (plus a b) (+ a b)) (plus 2 2))\n')
    for options, status in [((), 0), (('--deny-warnings',), 1),
                            (('--deny', 'liftable-function'), 1),
                            (('--allow', 'liftable-function'), 0)]:
        t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--format=short', *options, path]), status=status)
    t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'explain', 'L0002']), contains=['liftable-function', 'def-'])
    path.write_text('(def broken\n')
    result = t.expect(t.run([ROOT / 'otium', '--runtime=' + t.runtime, 'lint', '--format=json', path]), status=1)
    t.check(json.loads(result.stdout)['code'] == 'E0001', result)
