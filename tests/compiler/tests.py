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


LIB = ROOT / 'lib'
PRIMITIVES = ROOT / 'c/src/primitives.ot'
IR_CHECK = ROOT / 'tests/compiler/ir-check.ot'


def ir_check(t, *arguments):
    t.expect(t.otium('--no-project', '-L', LIB, IR_CHECK, '--', *arguments),
             contains=[' 0 failures'])


@test('compiler ir boot sources')
def ir_boot(t):
    """The verifier, text form, copying and rescope over the boot sources."""
    ir_check(t, 'boot', LIB, PRIMITIVES)


@test('compiler ir programs')
def ir_programs(t):
    ir_check(t, 'programs', *PROGRAMS)


@test('compiler ir texts')
def ir_texts(t):
    """Hand-written trees, and bad trees the verifier rejects."""
    ir_check(t, 'texts')


@test('compiler verify mode', timeout=120)
def verify_mode(t):
    """OTIUM_OPT_VERIFY=1 checks and rescopes trees without changing the code."""
    env = {'OTIUM_OPT_VERIFY': '1'}
    out = t.scratch / 'verified.boot'
    t.expect(t.otium('--no-project', '-L', LIB, '-e',
                     f'(require otium.compiler.boot :as boot) '
                     f'(boot/build-standard "{out}" "{LIB}" "{PRIMITIVES}") (exit 0)', env=env))
    built = Path(os.environ['OTIUM_BUILD']) / 'otium.boot'
    t.check(out.read_bytes() == built.read_bytes(), 'the boot file built with verifying differs')
    for program in PROGRAMS:
        plain = t.otium(program)
        verified = t.expect(t.otium(program, env=env), status=plain.status)
        t.check(verified.stdout == plain.stdout and verified.stderr == plain.stderr,
                f'{program.name}: output differs with verifying')


@test('compiler known table')
def known_table(t):
    """Every known-function entry names a core cell with the stated arity."""
    t.expect(t.otium('--no-project', '-L', LIB, ROOT / 'tests/compiler/known-check.ot'),
             contains=[' 0  failures'])


@test('compiler inline bodies')
def inline_bodies(t):
    """The boot file keeps every inline candidate's body, which parses back and
    verifies. A VM process loads core from source, so has none."""
    t.expect(t.otium('--no-project', '-L', LIB, '-e',
                     '(require otium.compiler.ir :as ir) (require otium.compiler.known :as known) '
                     '(for ((name :in (if (%native?) known/inline-candidates (quote ())))) '
                     '  (let ((text (get %inline-bodies name))) '
                     '    (unless text (println "missing " name)) '
                     '    (when text '
                     '      (ir/verify (ir/text->tree text)) '
                     '      (unless (equal? (ir/tree->text (ir/text->tree text)) text) (println "differs " name))))) '
                     '(println "checked") (exit 0)'),
             stdout='checked\n')
@test('compiler inline pass')
def inline_pass(t):
    """Hand-written trees through the inline pass, text in and text out."""
    t.expect(t.otium('--no-project', '-L', LIB, ROOT / 'tests/compiler/inline-check.ot'),
             contains=[' 0 failures'])
