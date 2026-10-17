#!/usr/bin/env python3
"""Run portable library, module, reload, and example contracts on both engines."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def run(runtime, path, *options, expected=None, stress=False):
    command = ([str(ROOT/'c/otium')] if runtime == 'c' else
               [os.environ.get('CHEZ', 'chez'), '--libdirs', str(ROOT/'chez'),
                '--script', str(ROOT/'chez/main.ss')])
    command.append('--no-project')
    env = dict(os.environ)
    if runtime == 'c' and stress:
        env['OTIUM_GC_STRESS'] = '1'
    result = subprocess.run(command + list(options) + [str(path)], cwd=ROOT,
                            env=env, text=True, capture_output=True, timeout=60)
    assert result.returncode == 0, (runtime, path, result.stdout, result.stderr)
    if expected is not None:
        assert result.stdout == expected, (runtime, path, result.stdout, expected)
    return result.stdout

with tempfile.TemporaryDirectory(prefix='otium-library-') as tmp:
    directory = Path(tmp)
    module = directory / 'portable.ot'
    module.write_text('(ns portable)\n(defonce state #f)\n(def (value) "module value" 7)\n(def (where) (current-ns))\n(def (help) (describe (quote value)))\n')
    (directory / 'portable.scm').write_text('(error "wrong suffix")\n')
    payload = directory / 'payload.txt'
    payload.write_text('é🙂\n', encoding='utf-8')
    script = directory / 'modules.scm'
    script.write_text('''
(require portable :as p)
(def (caller) (p/value))
(def saved p/value)
(set! p/value (fn () 9))
(println (caller) (saved) p/state)
(require (portable :as p :reload))
(println (caller) p/state)
(require '(portable :as q :refer (value)))
(println (q/value) (value))
(ns consumer (:require (portable :as p)))
(println (p/value))
(println (current-ns))
(println (describe 'p/value) (describe 'missing))
(println (p/where) (p/help))
''')
    for runtime in ('c', 'chez'):
        for fixture in ('bootstrap', 'sequences', 'reflection', 'printing'):
            path = ROOT/'shared/tests/library'/(fixture+'.scm')
            run(runtime, path, stress=True, expected=path.with_suffix('.txt').read_text())
        output = run(runtime, ROOT/'shared/tests/library/common.scm', stress=True)
        assert output.startswith('library checks:'), output
        run(runtime, script, '--path', str(directory), expected='9 7 #f\n7 #f\n7 7\n7\nconsumer\nportable/value: module value nil\nconsumer nil\n')
        read = directory/'read.scm'
        import json
        read.write_text('(print (read-file '+json.dumps(str(payload))+'))\n')
        run(runtime, read, expected='é🙂\n')
        run(runtime, ROOT/'shared/tests/otcl.scm', '--path', str(ROOT/'examples/otcl'), expected='')
        run(runtime, ROOT/'examples/reload.scm', '--path', str(ROOT/'examples'),
            expected='First callback: 1\nCallback after reload, with preserved state: 2\n')
        for example in ('multimethods/animals.ot', 'multimethods/events.scm',
                        'hand/animals-multimethods.ot', 'hand/lc-1-two-sum.ot',
                        'hand/lc-3622-check-divis.ot', 'hand/lc-2778-sum-of-squares.ot'):
            path = ROOT/'examples'/example
            actual = run(runtime, path)
            if runtime == 'c':
                (directory/(path.name+'.out')).write_text(actual)
            else:
                assert actual == (directory/(path.name+'.out')).read_text(), (example, actual)
        print(runtime+': '+output.strip()+'; modules, reload, file IO, OTCL and examples passed')
