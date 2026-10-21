"""The shared library, modules, reload and file input on both runtimes."""
from harness import BOTH, case, test

for name in ('bootstrap', 'sequences', 'reflection', 'printing'):
    case('library ' + name, f'tests/library/{name}.ot', stdout_file=f'tests/library/{name}.txt')
case('library', 'tests/library/common.ot')
case('library arithmetic', 'tests/library/arithmetic.ot')
case('library chars', 'tests/library/chars.ot')
case('library modules', 'tests/library/modules.ot', paths=['tests/library/modules'],
     stdout='9 7 #f\n7 #f\n7 7\n7\nconsumer\nportable/value: module value nil\nconsumer nil\n')
case('library read-file', 'tests/library/read-file.ot', stdout='é🙂\n')
case('example reload', 'examples/reload.ot', paths=['examples'],
     stdout='First callback: 1\nCallback after reload, with preserved state: 2\n')

EXAMPLES = ['multimethods/animals.ot', 'multimethods/events.ot', 'hand/animals-multimethods.ot',
            'hand/lc-1-two-sum.ot', 'hand/lc-3622-check-divis.ot', 'hand/lc-2778-sum-of-squares.ot']


@test('examples agree', needs=['chez'])
def examples_agree(t):
    for example in EXAMPLES:
        c, chez = (t.expect(t.otium('examples/' + example, runtime=runtime)) for runtime in BOTH)
        t.check(c.stdout == chez.stdout, f'{example}: C and Chez output differ\n{c}\n{chez}')
