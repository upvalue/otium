"""Module search order, suffixes, failed and circular loads, reload, and the core lock."""
from harness import case, test

case('modules', 'tests/modules/checks.ot', paths=['tests/modules/first', 'tests/modules/second'])
case('core lock', 'tests/modules/core-lock.ot')


@test('core unlock')
def core_unlock(t):
    """Defining in otium.core fails on both paths unless --c.unlock-core or OTIUM_UNLOCK_CORE."""
    program = 'tests/modules/core-define.ot'
    for mode in ([], ['--c.vm']):
        t.expect(t.otium(*mode, program), status=1,
                 stderr=['E0209', '`clamp` is in otium.core', '--c.unlock-core'])
        t.expect(t.otium(*mode, '--c.unlock-core', program), stdout=':assigned')
        t.expect(t.otium(*mode, program, env={'OTIUM_UNLOCK_CORE': '1'}), stdout=':assigned')
