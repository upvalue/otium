"""Cleanup failures replace pending quit and interrupt control transfers."""
import os
import select
import signal

from harness import case, test

case('resource quit', 'tests/resource/quit.ot', status=1, stdout='released\n', stderr=['cleanup'])


@test('resource interrupt')
def interrupt(t):
    process = t.spawn(t.command('tests/resource/interrupt.ot'), text=True)
    ready, _, _ = select.select([process.stdout], [], [], 10)
    t.check(ready, 'did not reach the resource body')
    first = os.read(process.stdout.fileno(), 1).decode()
    t.check(first == 'x', 'exited before the resource body')
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=10)
    output = first + stdout
    suffix = 'released\n'
    t.check(output.endswith(suffix) and set(output[:-len(suffix)]) == {'x'},
            'cleanup did not run once after the marker', output[-200:], stderr)
    t.check(process.returncode != 0, 'interrupted process exited 0')
    t.check('cleanup' in stderr, 'cleanup error was discarded', stderr)
