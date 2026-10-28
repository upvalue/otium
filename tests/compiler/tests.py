"""The native compiler: programs give the same output on the VM and natively,
and the compiler built by itself matches the one the VM built."""
import os
from pathlib import Path

from harness import ROOT, test

PROGRAMS = sorted((ROOT / 'tests/compiler/programs').glob('*.ot'))


@test('compiler differential')
def differential(t):
    for program in PROGRAMS:
        vm = t.otium('--c.vm', program)
        native = t.expect(t.otium(program), status=vm.status)
        t.check(native.stdout == vm.stdout, f'{program.name}: stdout differs',
                f'--- vm\n{vm.stdout}--- native\n{native.stdout}')
        t.check(native.stderr == vm.stderr, f'{program.name}: stderr differs',
                f'--- vm\n{vm.stderr}--- native\n{native.stderr}')


@test('compiler stage 2', timeout=120)
def stage2(t):
    out = t.scratch / 'stage2.boot'
    lib = ROOT / 'lib'
    primitives = ROOT / 'c/src/primitives.ot'
    t.expect(t.otium('--no-project', '-L', lib, '-e',
                     f'(require otium.compiler.boot :as boot) '
                     f'(boot/build-standard "{out}" "{lib}" "{primitives}") (exit 0)'))
    built = Path(os.environ['OTIUM_BUILD']) / 'otium.boot'
    t.check(out.read_bytes() == built.read_bytes(), 'the natively built boot file differs')
