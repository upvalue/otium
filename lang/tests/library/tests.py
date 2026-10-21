"""The shared library, modules, reload and file input on both runtimes."""
from harness import BOTH, case, test

for name in ('bootstrap', 'sequences', 'reflection', 'printing'):
    case('library ' + name, f'tests/library/{name}.ot', stdout_file=f'tests/library/{name}.txt')
case('library', 'tests/library/common.ot')
case('library arithmetic', 'tests/library/arithmetic.ot')
case('library chars', 'tests/library/chars.ot')
case('library bytevectors', 'tests/library/bytevectors.ot')
case('library resources', 'tests/library/resources.ot')
case('library ports', 'tests/library/ports.ot')
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


@test('library port datum reader', runtimes=BOTH)
def port_reader(t):
    t.expect(t.otium('-L', 'tests/reader/data', 'tests/library/port-reader.ot'),
             stdout='port datum reader: 165 checks passed\n')


@test('library port exit flush', runtimes=BOTH)
def port_exit_flush(t):
    from pathlib import Path
    path = str(Path(t.scratch) / 'exit-output')
    t.expect(t.otium('tests/library/port-exit.ot', '--', path), stdout='')
    t.check(Path(path).read_text() == 'flushed on exit', 'end-of-script did not flush')
    t.expect(t.otium('tests/library/port-exit.ot', '--', path, 'quit'), status=7, stdout='')
    t.check(Path(path).read_text() == 'flushed on exit', 'quit did not flush')
