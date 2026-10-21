"""Manse state and injected failure paths, without a display or Ghostty library."""
from harness import case

for name in ('apps', 'strip', 'settings', 'fonts', 'fit', 'theme', 'pty-read',
             'pty-write', 'pty-reap', 'construct-cleanup', 'pty-construct-cleanup',
             'ghostty-cleanup', 'grapheme-capacity'):
    case('manse ' + name, 'manse/tests/' + name + '.ot', paths=['.'],
         runtimes=['chez'], needs=['ffi'])
