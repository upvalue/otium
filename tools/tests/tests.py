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
    ports = source.split('static const nat_def port_natives[] = {')[1].split('};')[0]
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


@test('namespace reference', needs=['chez'])
def namespace_reference(t):
    """Export public bindings and module prose without loading the UI backend."""
    (t.scratch / 'lib').symlink_to(ROOT / 'lib', target_is_directory=True)
    (t.scratch / 'docs').mkdir()
    t.expect(t.run([ROOT / 'otium', '--runtime=chez', ROOT / 'tools/export-reference.ot'],
                   cwd=t.scratch))
    generated = (t.scratch / 'docs/reference.json').read_text()
    t.check(generated == (ROOT / 'docs/reference.json').read_text(), 'reference needs regeneration')
    namespaces = {ns['name']: ns for ns in json.loads(generated)['namespaces']}
    t.check(set(namespaces) == {'otium.core', 'otium.ui'})
    for ns in namespaces.values():
        t.check(bool(ns['doc']), 'missing module docstring:', ns['name'])
        names = [entry['name'] for entry in ns['entries']]
        t.check(names == sorted(set(names)), 'entries must be unique and sorted')
        for entry in ns['entries']:
            if 'source' in entry:
                source = entry['source']
                line = (ROOT / source['file']).read_text().splitlines()[source['line'] - 1]
                t.check(entry['name'] in line, 'incorrect source link:', entry['name'])
    ui = {entry['name']: entry for entry in namespaces['otium.ui']['entries']}
    t.check('window-call' not in ui, 'private helper exported')
    t.check(ui['measure']['signatures'][0]['text'] == '(measure widget limits)')
    t.check(ui['run']['signatures'][0]['text'] == '(run)')
    t.check(ui['Widget']['kind'] == 'value')
    t.check('Resolve a palette keyword' in ui['color']['doc'])
    t.check('`reconcile!`' in namespaces['otium.ui']['doc'])
    core = {entry['name']: entry for entry in namespaces['otium.core']['entries']}
    t.check('+' in core and 'map' in core, 'core must include builtins and prelude')
