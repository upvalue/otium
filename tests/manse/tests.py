"""Manse state and injected failure paths, without a display or Ghostty library."""
from harness import BOTH, case, test

for name in ('apps', 'strip', 'settings', 'fit', 'theme', 'pty-write',
             'pty-reap', 'construct-cleanup', 'pty-construct-cleanup',
             'ghostty-cleanup', 'grapheme-capacity'):
    case('manse ' + name, 'manse/tests/' + name + '.ot', paths=['.'],
         runtimes=BOTH, needs=['ffi'])


@test('manse fonts', runtimes=BOTH, needs=['ffi'])
def fonts(t):
    t.expect(t.otium('-L', '.', 'manse/tests/fonts.ot', '--', t.scratch))
