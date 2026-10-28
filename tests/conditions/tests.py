"""Condition semantics, exit through handlers and cleanup, and SIGINT."""
import os
import select
import signal

from harness import case, test

SUPPORT = ['--load', 'tests/conditions/support.ot']

case('conditions resource exit', 'tests/conditions/resource-exit.ot', status=7, stdout=':quit\n')

case('conditions', 'tests/conditions/common.ot', options=SUPPORT)
case('conditions exit', 'tests/conditions/exit.ot', options=SUPPORT, status=7,
     stdout='condition checks: 7\n')
case('conditions quit', 'tests/conditions/quit.ot', options=SUPPORT, status=7,
     stdout='condition checks: 7\n')
case('conditions cleanup error', 'tests/conditions/cleanup-error.ot', options=SUPPORT,
     stdout='condition checks: 8\n')


@test('conditions interrupt')
def interrupt(t):
    process = t.spawn(t.command(*SUPPORT, 'tests/conditions/interrupt.ot'), text=True)
    ready, _, _ = select.select([process.stdout], [], [], 10)
    t.check(ready, 'did not reach the protected body')
    first = os.read(process.stdout.fileno(), 1).decode()
    t.check(first == 'x', 'exited before the protected body')
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=10)
    output = first + stdout
    suffix = 'condition checks: 9\n'
    t.check(output.endswith(suffix) and set(output[:-len(suffix)]) == {'x'},
            'cleanup did not run once after the marker', output[-200:], stderr)
    t.check(process.returncode != 0, 'interrupted process exited 0')


@test('conditions blocking port interrupt')
def port_interrupt(t):
    process = t.spawn(t.command('tests/conditions/port-interrupt.ot'), text=True)
    ready, _, _ = select.select([process.stdout], [], [], 10)
    t.check(ready, 'did not reach blocking read')
    t.check(process.stdout.readline() == 'waiting\n', 'missing read marker')
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=10)
    t.check(':quit\n' in stdout, 'resource cleanup missed interrupt outcome', stdout, stderr)
    t.check(process.returncode != 0, 'interrupted read exited successfully')
