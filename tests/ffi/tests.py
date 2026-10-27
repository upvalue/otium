"""Foreign calls against libm and an ABI fixture that Meson builds twice."""
from harness import BOTH, defaults, library, test

defaults(suite='native', needs=['ffi'])


def ffi(t, program, *arguments, paths=(), **expected):
    fixtures = [library('ffi-fixture-17'), library('ffi-fixture-29')]
    options = ['--load', 'tests/ffi/support.ot', '--load', f'tests/ffi/runtime-{t.runtime}.ot']
    options += [part for path in paths for part in ('-L', path)]
    return t.expect(t.otium(*options, program, '--', *fixtures, *arguments), **expected)


@test('ffi math', runtimes=BOTH)
def math(t):
    t.expect(t.otium('examples/math.ot'), stdout='1.0\n5.0\n0.75\n12.0\n')


@test('ffi', runtimes=BOTH)
def fixture(t):
    ffi(t, 'tests/ffi/common.ot', stdout='ffi: 222 checks passed\n')


@test('ffi callbacks', runtimes=BOTH)
def callbacks(t):
    ffi(t, 'tests/ffi/callbacks.ot', t.scratch, paths=[t.scratch], stdout='ffi callbacks: 51 checks passed\n')


@test('ffi callback exit', runtimes=BOTH)
def callback_exit(t):
    ffi(t, 'tests/ffi/callback-exit.ot', status=3, stdout='before\n')


@test('ffi callback thread', runtimes=BOTH)
def callback_thread(t):
    ffi(t, 'tests/ffi/callback-thread.ot', status=None, stdout='',
        stderr=['thread other than the Otium thread'])


@test('ffi ownership', runtimes=BOTH)
def ownership(t):
    t.expect(t.otium('tests/ffi/ownership.ot'), stdout='own: 10 checks passed\n')


@test('ffi bulk', runtimes=BOTH)
def bulk(t):
    t.expect(t.otium('tests/ffi/bulk.ot'))
