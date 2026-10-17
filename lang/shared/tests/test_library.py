#!/usr/bin/env python3
"""Run portable library, module, reload, and example contracts on both engines."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from otium_tools import runtime_argument, runtimes, command as runtime_command

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
runtime_argument(parser)
args = parser.parse_args()


def run(runtime, path, *options, expected=None, stress=False):
    command = runtime_command(runtime)
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
    script = directory / 'modules.ot'
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
    for runtime in runtimes(args.runtime):
        for fixture in ('bootstrap', 'sequences', 'reflection', 'printing'):
            path = ROOT/'shared/tests/library'/(fixture+'.ot')
            run(runtime, path, stress=True, expected=path.with_suffix('.txt').read_text())
        output = run(runtime, ROOT/'shared/tests/library/common.ot', stress=True)
        assert output.startswith('library checks:'), output
        arithmetic = run(runtime, ROOT/'shared/tests/library/arithmetic.ot', stress=True)
        assert arithmetic.startswith('arithmetic checks:'), arithmetic
        run(runtime, script, '--path', str(directory), expected='9 7 #f\n7 #f\n7 7\n7\nconsumer\nportable/value: module value nil\nconsumer nil\n')
        read = directory/'read.ot'
        import json
        read.write_text('(print (read-file '+json.dumps(str(payload))+'))\n')
        run(runtime, read, expected='é🙂\n')
        run(runtime, ROOT/'shared/tests/otcl.ot', '--path', str(ROOT/'examples/otcl'), expected='')
        run(runtime, ROOT/'examples/reload.ot', '--path', str(ROOT/'examples'),
            expected='First callback: 1\nCallback after reload, with preserved state: 2\n')
        for example in ('multimethods/animals.ot', 'multimethods/events.ot',
                        'hand/animals-multimethods.ot', 'hand/lc-1-two-sum.ot',
                        'hand/lc-3622-check-divis.ot', 'hand/lc-2778-sum-of-squares.ot'):
            path = ROOT/'examples'/example
            actual = run(runtime, path)
            if runtime == 'c':
                (directory/(path.name+'.out')).write_text(actual)
            else:
                assert actual == (directory/(path.name+'.out')).read_text(), (example, actual)
        print(runtime+': '+output.strip()+'; modules, reload, file IO, OTCL and examples passed')
