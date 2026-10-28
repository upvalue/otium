"""Reader fixtures, values, module loading, read errors and the REPL."""
from harness import case, test

case('reader values', 'tests/reader/values.ot')
case('reader datums', 'tests/reader/datums.ot', paths=['tests/reader/data'])
case('reader tokens', 'tests/reader/tokens.ot', paths=['tests/reader/data'],
     stdout_file='tests/reader/tokens.txt')
case('reader module', 'tests/reader/module.ot', paths=['tests/reader/modules'], stdout=':module 7\n')


@test('reader errors')
def errors(t):
    for name in ('vector', 'bracket', 'escape', 'shebang'):
        path = f'tests/reader/invalid/{name}.ot'
        t.expect(t.otium(path), status=1, stderr=[path])


@test('reader repl')
def repl(t):
    t.expect(t.otium('--repl', stdin="nil\n(println\n [nil :repl])\n(println (read-string \"'[]\"))\n"),
             contains=['nil\n', '[nil :repl]', '(quote (array))'])
