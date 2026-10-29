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


def expected_boot():
    """The boot file a build in this configuration should match, or None when
    none does: with a component switched off, in check mode, or with another
    inlining budget, the build differs from both boot files by design."""
    opt = os.environ.get('OTIUM_OPT', '')
    budget = os.environ.get('OTIUM_OPT_INLINE_BUDGET', '100')
    if os.environ.get('OTIUM_OPT_CHECK') == '1' or opt not in ('', 'all', 'none') or budget != '100':
        return None
    name = 'otium-noopt.boot' if opt == 'none' else 'otium.boot'
    return Path(os.environ['OTIUM_BUILD']) / name


@test('compiler stage 2', timeout=120)
def stage2(t):
    out = t.scratch / 'stage2.boot'
    lib = ROOT / 'lib'
    primitives = ROOT / 'c/src/primitives.ot'
    t.expect(t.otium('--no-project', '-L', lib, '-e',
                     f'(require otium.compiler.boot :as boot) '
                     f'(boot/build-standard "{out}" "{lib}" "{primitives}") (exit 0)'))
    built = expected_boot()
    t.check(built is None or out.read_bytes() == built.read_bytes(), 'the natively built boot file differs')


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


@test('compiler types pass')
def types_pass(t):
    """The types pass's rewrites, and rewrites it must not make."""
    t.expect(t.otium('--no-project', '-L', LIB, ROOT / 'tests/compiler/types-check.ot'),
             contains=[' 0 failures'])


@test('compiler check mode')
def check_mode(t):
    """OTIUM_OPT_CHECK=1 keeps unchecked operations' checks, and a failed one traps."""
    if os.environ.get('OTIUM_VM'):
        return
    out = t.expect(t.otium('--no-project', '-L', LIB, ROOT / 'tests/compiler/proof-trap.ot',
                           env={'OTIUM_OPT_CHECK': '1'}))
    t.check(out.stdout == 'optimizer: type proof failed\n' * 3, 'check mode did not trap',
            out.stdout)


@test('compiler verify mode', timeout=120)
def verify_mode(t):
    """OTIUM_OPT_VERIFY=1 checks and rescopes trees without changing the code."""
    env = {'OTIUM_OPT_VERIFY': '1'}
    out = t.scratch / 'verified.boot'
    t.expect(t.otium('--no-project', '-L', LIB, '-e',
                     f'(require otium.compiler.boot :as boot) '
                     f'(boot/build-standard "{out}" "{LIB}" "{PRIMITIVES}") (exit 0)', env=env))
    built = expected_boot()
    t.check(built is None or out.read_bytes() == built.read_bytes(),
            'the boot file built with verifying differs')
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
    """Hand-written trees through the inline pass, text in and text out, at
    the default inlining budget the expected trees assume."""
    t.expect(t.otium('--no-project', '-L', LIB, ROOT / 'tests/compiler/inline-check.ot',
                     env={'OTIUM_OPT_INLINE_BUDGET': '100'}),
             contains=[' 0 failures'])


@test('compiler notes')
def notes(t):
    """otium compile --notes reports generic arithmetic, checks and closures
    in loops, and calls left out of inlining, without running the program."""
    if os.environ.get('OTIUM_VM'):
        return
    fixture = ROOT / 'tests/compiler/notes.ot'
    out = t.expect(t.otium('--no-project', '-L', LIB, ROOT / 'lib/otium/tools/compile.ot', '--', '--notes', fixture),
                   contains=[f'{fixture}:7:15: note: `cdr` checks for a pair each time round this loop',
                             f'{fixture}:7:26: note: `+` stays generic: the first operand is a number',
                             f'{fixture}:11:42: note: a closure is made each time round this loop',
                             f'{fixture}:13:15: note: `inc` isn\'t inlined: it\'s called with the wrong number of arguments'])
    t.check('not run' not in out.stdout, 'the program ran', out.stdout)

