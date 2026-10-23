"""Launcher options, program arguments, search paths and the portable CLI library."""
import shutil

from harness import BOTH, ROOT, case, test

case('cli', 'tests/cli/common.ot')

BAD_OPTIONS = [('--unknown',), ('--c.heap-mxa=1024',), ('--c.heap-max', '1024'),
               ('--c.heap-max=-1',), ('--c.heap-max=0',), ('--c.heap-max= 12',),
               ('--c.heap-max=18446744073709551616',), ('--c.max-depth=4294967296',),
               ('--help=yes',), ('--path',), ('--',), ('a.ot', 'b.ot'),
               ('--project', 'x', '--no-project')]


@test('cli help', needs=['chez'])
def help_parity(t):
    c, chez = (t.expect(t.otium('--help', runtime=runtime, project=True)) for runtime in BOTH)
    t.check(c.stdout == chez.stdout, 'C and Chez --help differ', c, chez)


@test('cli options', runtimes=BOTH)
def options(t):
    for bad in BAD_OPTIONS:
        t.expect(t.otium(*bad, project=True), status=2)
    help_text = t.expect(t.otium('--help', project=True)).stdout
    t.expect(t.otium('tests/cli/arguments.ot', '--help', project=True), stdout=help_text)


@test('cli arguments', runtimes=BOTH)
def arguments(t):
    script = ROOT / 'tests/cli/arguments.ot'
    tokens = ['--help', '', 'two words', 'λ', '--', '-file', '--c.heap-max=1']
    expected = ''.join(f'{len(arg)}:{arg}\n' for arg in tokens) + f'{script}\n'
    t.expect(t.otium('--c.heap-init=1048576', '--c.heap-max=67108864', script, '--', *tokens),
             stdout=expected)
    t.expect(t.otium(script), stdout=f'{script}\n')
    t.expect(t.otium('--load', 'tests/cli/pre.ot', script), stdout=f'pre\n{script}\n')
    t.expect(t.otium('--load', 'tests/cli/pre.ot'), stdout='pre\n')


@test('cli paths', runtimes=BOTH)
def paths(t):
    script = 'tests/cli/sample.ot'
    t.expect(t.otium('--path', 'tests/cli/lib', script), stdout='42\n')
    t.expect(t.otium('-L', 'tests/cli/lib', script), stdout='42\n')
    t.expect(t.otium(script, env={'OTIUM_PATH': str(ROOT / 'tests/cli/lib')}), stdout='42\n')
    # project.ot is found from the working directory upward, or named with --project.
    shutil.copytree(ROOT / 'tests/cli/lib', t.scratch / 'lib')
    (t.scratch / 'project.ot').write_text('(paths "lib")')
    (t.scratch / 'child').mkdir()
    t.expect(t.otium(ROOT / script, project=True, cwd=t.scratch / 'child'), stdout='42\n')
    t.expect(t.otium('--project', t.scratch / 'project.ot', ROOT / script, project=True, cwd='/tmp'),
             stdout='42\n')


@test('cli greet', runtimes=BOTH)
def greet(t):
    example = 'examples/greet.ot'
    t.expect(t.otium(example, '--', '-n2', 'Alice'), stdout='Hello, Alice!\n' * 2)
    t.expect(t.otium(example, '--', '--help'), contains=['Number of greetings', 'default: 1'])
    t.expect(t.otium(example, '--', '--version'), stdout='0.1.0\n')
    t.expect(t.otium(example, '--', '--count=nope', 'Alice'), status=2, stdout='',
             stderr=['count', 'nope'])


@test('cli inactive backend options', runtimes=['chez'])
def inactive(t):
    """C-only settings are accepted, validated and ignored by Chez."""
    result = t.otium('--c.gc-stats', '--c.gc-force-compact', '--c.heap-max=1024',
                     'examples/greet.ot', '--', 'Alice')
    t.expect(result, stdout='Hello, Alice!\n')
    t.check(result.stderr == '', 'unexpected stderr', result)
