import ctypes.util

from harness import BOTH, Skip, test


@test('sqlite raw', runtimes=BOTH, suite='native', needs=['ffi'])
def raw(t):
    if not ctypes.util.find_library('sqlite3'):
        raise Skip('system SQLite library not found')
    t.expect(t.otium('-L', 'lib', 'tests/sqlite/checks.ot'),
             stdout='sqlite:  96  checks passed\n')


@test('sqlite high-level', runtimes=BOTH, suite='native', needs=['ffi'])
def high_level(t):
    if not ctypes.util.find_library('sqlite3'):
        raise Skip('system SQLite library not found')
    t.expect(t.otium('-L', 'lib', 'tests/sqlite/high-level.ot'),
             stdout='sqlite high-level: 67 checks passed\n')
