"""Reader fixtures, values, module loading, read errors and the REPL."""
from harness import BOTH, case, test

case('reader values', 'tests/reader/values.ot')
case('reader datums', 'tests/reader/datums.ot', paths=['tests/reader/data'])
case('reader chez', 'tests/reader/chez.ot', runtimes=['chez'], paths=['tests/reader/data'])
case('reader module', 'tests/reader/module.ot', paths=['tests/reader/modules'], stdout=':module 7\n')


@test('reader errors', runtimes=BOTH)
def errors(t):
    for name in ('vector', 'bracket', 'escape', 'shebang'):
        path = f'tests/reader/invalid/{name}.ot'
        t.expect(t.otium(path), status=1, stderr=[path])


@test('reader repl', runtimes=BOTH)
def repl(t):
    t.expect(t.otium('--repl', stdin="nil\n(println\n [nil :repl])\n(println (read-string \"'[]\"))\n"),
             contains=['nil\n', '[nil :repl]', '(quote (array))'])


@test('reader tokens', runtimes=BOTH)
def tokens(t):
    t.expect(t.otium('-L', 'tests/reader/data', 'tests/reader/tokens.ot'),
             contains=['reader tokens:'])


@test('reader tokens agree', needs=['chez'])
def tokens_agree(t):
    results = [t.expect(t.otium('-L', 'tests/reader/data', 'tests/reader/tokens.ot', runtime=r))
               for r in BOTH]
    t.check(results[0].stdout == results[1].stdout, 'token fields differ', *results)
