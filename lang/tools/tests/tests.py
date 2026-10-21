"""The launcher, generated files, the runtime reference and doc locations."""
import json
import re
import shutil
import sys

from harness import ROOT, test


def launcher(t):
    """A copy of ./otium whose runtimes are stubs that print their arguments as JSON."""
    root = t.scratch / 'otium launcher'
    root.mkdir()
    shutil.copy2(ROOT / 'otium', root / 'otium')
    for runtime, path in (('c', 'build/otium'), ('chez', 'chez/run')):
        stub = root / path
        stub.parent.mkdir()
        stub.write_text('#!/usr/bin/env python3\nimport json,sys\n'
                        f'print(json.dumps([{runtime!r}, sys.argv[1:]]))\n')
        stub.chmod(0o755)
    env = {'OTIUM_BUILD': None}

    def run(*args, status=0):
        result = t.run([root / 'otium', *args], cwd='/', env=env)
        t.expect(result, status=status)
        return json.loads(result.stdout) if status == 0 else None
    return root, run


@test('launcher')
def launcher_dispatch(t):
    root, run = launcher(t)
    t.check(run() == ['chez', []])
    t.check(run('--runtime=c', 'x.ot') == ['c', ['x.ot']])
    t.check(run('--runtime=chez', 'x.ot') == ['chez', ['x.ot']])
    t.check(run('x.ot', '--runtime', 'chez') == ['chez', ['x.ot']])
    args = ['--path', 'with spaces', 'program.ot', '--', '', 'λ', '--runtime=c', '--', '--help']
    t.check(run('--runtime=chez', *args) == ['chez', args], 'arguments were not preserved')
    tools = str(root / 'lib/otium/tools')
    t.check(run('fmt', 'a.ot', '--check') ==
            ['chez', ['--no-project', f'{tools}/fmt.ot', '--', 'a.ot', '--check']], 'fmt')
    for tool in ('lint', 'explain'):
        t.check(run(tool, 'a.ot') == ['chez', [f'{tools}/{tool}.ot', '--', 'a.ot']], tool)
    for option in ('--path', '-L', '--project', '--load'):
        args = [option, '--runtime=chez', 'x.ot']
        t.check(run(*args) == ['chez', args], f'{option} value was taken as a selector')
    for args in (('--runtime',), ('--runtime=',), ('--runtime=bad',), ('--runtime', 'bad')):
        run(*args, status=2)


@test('generated')
def generated(t):
    for tool in ('generate-cli.py', 'generate-reference.py', 'generate-diagnostics.py'):
        t.expect(t.run([sys.executable, ROOT / 'tools' / tool, '--check']))


@test('runtime reference')
def reference(t):
    """Every C native and special form is documented, and prelude definitions have docstrings."""
    source = (ROOT / 'c/src/otium.c').read_text()
    natives = source.split('static const nat_def core_nats[] = {')[1].split('};')[0]
    ports = (ROOT / 'c/src/ot-ports.inc').read_text().split('static const nat_def port_natives[] = {')[1].split('};')[0]
    natives += ports
    specials = source.split('static bool special_name(')[1].split('};')[0]
    required = set(re.findall(r'\{"([^"]+)"', natives))
    required.update(re.findall(r'^\s*"([^"]+)"', specials, re.M))
    entries = json.loads((ROOT / 'lib/catalog/runtime.json').read_text())
    t.check(not required - entries.keys(), 'undocumented:', sorted(required - entries.keys()))
    prelude = (ROOT / 'lib/otium/core.ot').read_text()
    pattern = r'^\((?:define\s+\(([^\s()]+)[^\n]*?\)|defmacro\s+([^\s()]+)\s+\([^\n]*?\))'
    for match in re.finditer(pattern, prelude, re.M):
        name = match[1] or match[2]
        t.check(prelude[match.end():].lstrip().startswith('"'), 'missing source docstring:', name)
        t.check('c' not in entries.get(name, {}).get('runtimes', []), 'duplicate C documentation:', name)


@test('docs')
def docs(t):
    t.expect(t.run([ROOT / 'tools/check-docs']))
