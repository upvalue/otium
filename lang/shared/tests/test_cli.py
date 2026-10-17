#!/usr/bin/env python3
"""Check launcher parity and the portable CLI library through both runtimes."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COMMANDS = [
    [str(ROOT / 'c/otium')],
    [str(ROOT / 'chez/run')],
]


def run(command, *args, cwd=ROOT, env=None, status=0):
    result = subprocess.run([*command, *map(str, args)], cwd=cwd, env=env,
                            input='', text=True, capture_output=True, timeout=30)
    assert result.returncode == status, (command, args, result)
    return result


help_outputs = [run(cmd, '--help').stdout for cmd in COMMANDS]
assert help_outputs[0] == help_outputs[1], help_outputs
for cmd in COMMANDS:
    run(cmd, '--no-project', ROOT / 'shared/tests/cli/common.scm')
    for bad in [('--unknown',), ('--c.heap-mxa=1024',), ('--c.heap-max', '1024'),
                ('--c.heap-max=-1',), ('--c.heap-max=0',), ('--c.heap-max= 12',),
                ('--c.heap-max=18446744073709551616',), ('--c.max-depth=4294967296',),
                ('--help=yes',), ('--path',), ('--',),
                ('a.scm', 'b.scm'), ('--project', 'x', '--no-project')]:
        run(cmd, *bad, status=2)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        script = tmp / 'arguments.scm'
        script.write_text('''(require otium.process)
(write (otium.process/arguments)) (newline)
(display (otium.process/script-name)) (newline)
''')
        tokens = ['--help', '', 'two words', 'λ', '--', '-file', '--c.heap-max=1']
        expected = '("--help" "" "two words" "λ" "--" "-file" "--c.heap-max=1")\n' + str(script) + '\n'
        result = run(cmd, '--no-project', '--c.heap-init=1048576', '--c.heap-max=67108864', script, '--', *tokens)
        assert result.stdout == expected, result
        assert run(cmd, '--no-project', script).stdout == '()\n' + str(script) + '\n'
        assert run(cmd, script, '--help').stdout == help_outputs[0]
        pre = tmp / 'pre.scm'
        pre.write_text('(display "pre\\n")')
        assert run(cmd, '--no-project', '--load', pre, script).stdout.startswith('pre\n()\n')
        assert run(cmd, '--no-project', '--load', pre).stdout == 'pre\n'
        library = tmp / 'lib'
        library.mkdir()
        (library / 'sample.scm').write_text('(ns sample) (define answer 42)')
        script.write_text('(require sample) (write sample/answer) (newline)')
        assert run(cmd, '--no-project', '--path', library, script).stdout == '42\n'
        assert run(cmd, '--no-project', '-L', library, script).stdout == '42\n'
        env = dict(os.environ, OTIUM_PATH=str(library))
        assert run(cmd, '--no-project', script, env=env).stdout == '42\n'
        (tmp / 'project.ot').write_text('(paths "lib")')
        child = tmp / 'child'
        child.mkdir()
        assert run(cmd, script, cwd=child).stdout == '42\n'
        assert run(cmd, '--project', tmp / 'project.ot', script, cwd='/tmp').stdout == '42\n'
    example = ROOT / 'examples/shared/greet.scm'
    assert run(cmd, '--no-project', example, '--', '-n2', 'Alice').stdout == 'Hello, Alice!\n' * 2
    help_result = run(cmd, '--no-project', example, '--', '--help')
    assert 'Number of greetings' in help_result.stdout and 'default: 1' in help_result.stdout
    assert run(cmd, '--no-project', example, '--', '--version').stdout == '0.1.0\n'
    error = run(cmd, '--no-project', example, '--', '--count=nope', 'Alice', status=2)
    assert not error.stdout and 'count' in error.stderr and 'nope' in error.stderr
    print(f'{cmd[0]}: launcher and CLI checks passed')

# Inactive backend settings are accepted, validated, and silent.
result = run(COMMANDS[1], '--c.gc-stats', '--c.gc-force-compact', '--c.heap-max=1024',
             '--no-project', ROOT / 'examples/shared/greet.scm', '--', 'Alice')
assert result.stdout == 'Hello, Alice!\n' and result.stderr == '', result
# Check moving-GC roots used to install process arguments and parse a schema.
env = dict(os.environ, OTIUM_GC_STRESS='1')
run(COMMANDS[0], '--no-project', ROOT / 'shared/tests/cli/common.scm', env=env)
print('CLI parity and GC stress passed')
