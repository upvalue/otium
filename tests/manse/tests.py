"""Manse state, programs, Ghostty bindings and injected failure paths. Tests needing ui want a display."""
import os

from harness import Skip, case, test

for name in ('inspector', 'apps', 'strip', 'settings', 'fit', 'theme', 'pty-write',
             'pty-reap', 'construct-cleanup', 'pty-construct-cleanup',
             'ghostty-cleanup', 'grapheme-capacity', 'ghostty', 'keyboard',
             'paint-cells', 'pty', 'wheel', 'workspace'):
    case('manse ' + name, 'manse/tests/' + name + '.ot', paths=['.'], needs=['ffi'])
for name in ('paint-rows', 'text-render'):
    case('manse ' + name, 'manse/tests/' + name + '.ot', paths=['.'], needs=['ffi', 'ui'])


def threaded(name):
    @test('manse ' + name, needs=['ffi'])
    def run(t):
        if os.environ.get('OTIUM_VM'):
            raise Skip('threads need native code')
        t.expect(t.otium('-L', '.', 'manse/tests/' + name + '.ot'), contains=['checks passed'])


for name in ('program', 'development-repl'):
    threaded(name)


@test('manse development ui', suite='gpu', needs=['ui'], timeout=60, parallel=False)
def development_ui(t):
    t.expect(t.otium('-L', '.', 'manse/tests/development-ui.ot'), contains=['development UI:'])
