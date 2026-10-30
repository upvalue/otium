"""Manse state and injected failure paths, without a display or Ghostty library."""
import os

from harness import Skip, case, test

for name in ('apps', 'strip', 'settings', 'fit', 'theme', 'pty-write',
             'pty-reap', 'construct-cleanup', 'pty-construct-cleanup',
             'ghostty-cleanup', 'grapheme-capacity'):
    case('manse ' + name, 'manse/tests/' + name + '.ot', paths=['.'], needs=['ffi'])


@test('manse tty', needs=['ffi'])
def tty(t):
    if os.environ.get('OTIUM_VM'):
        raise Skip('threads need native code')
    t.expect(t.otium('-L', '.', 'manse/tests/tty.ot'), contains=['checks passed'])
