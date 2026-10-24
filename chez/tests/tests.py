"""Chez runtime unit tests, run as Chez scripts."""
from harness import chez, defaults, test

defaults(suite='chez', needs=['chez'])


def script(name, scratch):
    @test('chez ' + name)
    def run(t):
        command = [chez(), '--libdirs', 'chez', '--script', f'chez/tests/{name}.ss']
        t.expect(t.run(command + ([t.scratch] if scratch else [])))


# These take a scratch directory as their argument.
for name in ('tests', 'macro-tests', 'unary-call-tests', 'generic-tests'):
    script(name, scratch=True)
for name in ('optional-tests', 'append-tests', 'condition-tests', 'environment-tests', 'reader-tests', 'string-tests'):
    script(name, scratch=False)


@test('chez help')
def help_text(t):
    t.expect(t.run(['chez/run', '--help']))
