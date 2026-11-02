"""Manse state and injected failure paths, without a display or Ghostty library."""
import os

from harness import Skip, case, test

for name in ('inspector', 'development-repl', 'apps', 'strip', 'settings', 'fit', 'theme', 'pty-write',
             'pty-reap', 'construct-cleanup', 'pty-construct-cleanup',
             'ghostty-cleanup', 'grapheme-capacity', 'ghostty', 'keyboard',
             'paint-cells', 'pty', 'wheel', 'workspace'):
    case('manse ' + name, 'manse/tests/' + name + '.ot', paths=['.'], needs=['ffi'])
for name in ('paint-rows', 'text-render'):
    case('manse ' + name, 'manse/tests/' + name + '.ot', paths=['.'], needs=['ffi', 'ui'])


@test('manse tty', needs=['ffi'])
def tty(t):
    if os.environ.get('OTIUM_VM'):
        raise Skip('threads need native code')
    t.expect(t.otium('-L', '.', 'manse/tests/tty.ot'), contains=['checks passed'])


@test('manse development ui', suite='gpu', needs=['ui'], timeout=60, parallel=False)
def development_ui(t):
    t.expect(t.otium('-L', '.', 'manse/tests/development-ui.ot'), contains=['development UI:'])
