"""Green threads and actors. They need native code: on the VM only the error
is checked."""
import os
import select
import signal
import socket

from harness import Skip, test

VM = bool(os.environ.get('OTIUM_VM'))

# Test scheduler seeds; ':n' fixes the slice at n polls, else it is random.
SEEDS = ['1', '2', '3', '17', '99', '5:1', '6:3', '7:1000']


def native(t):
    if VM:
        raise Skip('threads need native code')


def threads_test(name):
    def register(function):
        def run(t):
            native(t)
            function(t)
        return test(name)(run)
    return register


def seeded(program):
    def run(t):
        for seed in SEEDS:
            result = t.otium(program, env={'OTIUM_SCHED': seed})
            t.check(result.status == 0 and result.stdout.endswith('checks passed\n'),
                    f'seed {seed} failed', result)
    return run


def real(program):
    def run(t):
        t.expect(t.otium(program), contains=['checks passed'])
    return run


threads_test('threads')(seeded('tests/threads/checks.ot'))
threads_test('threads real scheduler')(real('tests/threads/checks.ot'))
threads_test('actors')(seeded('tests/threads/actors.ot'))
threads_test('actors real scheduler')(real('tests/threads/actors.ot'))
threads_test('stack segments')(real('tests/threads/segments.ot'))


@threads_test('threads preemption')
def preemption(t):
    t.expect(t.otium('tests/threads/preempt.ot'), stdout='#t\n')


@threads_test('threads deadlock')
def deadlock(t):
    for env in ({}, {'OTIUM_SCHED': '4'}):
        t.expect(t.otium('tests/threads/deadlock.ot', env=env), status=None,
                 stderr=['E0503', 'deadlock.ot:8:1'])


@threads_test('threads exit')
def exit_from_thread(t):
    t.expect(t.otium('tests/threads/exit.ot'), status=3, stdout='main cleanup\n')


@threads_test('threads failure report')
def report(t):
    t.expect(t.otium('tests/threads/report.ot'), stdout=':failed\n',
             stderr=['error: worker broke', 'report.ot:5:3', '(error "worker broke")'])


@test('threads on the vm')
def on_vm(t):
    t.expect(t.otium('--c.vm', 'tests/threads/vm.ot'), status=None,
             stderr=['threads need native code'])


@threads_test('threads interrupt')
def interrupt(t):
    """SIGINT reaches the main thread while a worker spins."""
    process = t.spawn(t.command('tests/threads/interrupt.ot'), text=True)
    ready, _, _ = select.select([process.stdout], [], [], 10)
    t.check(ready and process.stdout.readline() == 'ready\n', 'did not start')
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=10)
    t.check('main cleanup' in stdout, 'the main thread did not unwind', stdout, stderr)
    t.check(process.returncode != 0, 'interrupted process exited 0')


@threads_test('threads idle interrupt')
def idle_interrupt(t):
    """SIGINT wakes the scheduler before its next timer expires."""
    process = t.spawn(t.command('tests/threads/idle-interrupt.ot'), text=True)
    ready, _, _ = select.select([process.stdout], [], [], 10)
    t.check(ready and process.stdout.readline() == 'ready\n', 'did not start')
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=2)
    t.check('main cleanup' in stdout, 'main did not unwind', stdout, stderr)
    t.check(process.returncode == 130, 'expected SIGINT exit status', stderr)


@threads_test('threads stdin parks')
def stdin_parks(t):
    """Input remains buffered across parking, including across buffer growth."""
    for seed in (None, *SEEDS):
        env = dict(os.environ)
        if seed:
            env['OTIUM_SCHED'] = seed
        process = t.spawn(t.command('tests/threads/read-stdin.ot'), text=True, env=env)
        process.stdin.write('x' * 5000)
        process.stdin.flush()
        ready, _, _ = select.select([process.stdout], [], [], 10)
        t.check(ready and process.stdout.readline() == 'ready\n',
                f'worker blocked behind stdin, seed {seed}')
        stdout, stderr = process.communicate('\n', timeout=10)
        t.check(process.returncode == 0 and stdout == '5000\n',
                f'input lost or duplicated, seed {seed}', stdout, stderr)


@threads_test('threads close waiting input')
def close_waiting_input(t):
    for seed in (None, *SEEDS):
        env = dict(os.environ)
        if seed:
            env['OTIUM_SCHED'] = seed
        process = t.spawn(t.command('tests/threads/close-input.ot'), text=True, env=env)
        ready, _, _ = select.select([process.stdout], [], [], 10)
        t.check(ready and process.stdout.readline() == ':closed\n',
                f'closed port did not wake reader, seed {seed}')
        stdout, stderr = process.communicate(timeout=10)
        t.check(process.returncode == 0, 'reader failed', stdout, stderr)


@threads_test('threads stdout parks')
def stdout_parks(t):
    for seed in (None, *SEEDS):
        env = dict(os.environ)
        if seed:
            env['OTIUM_SCHED'] = seed
        process = t.spawn(t.command('tests/threads/write-stdout.ot'), text=True, env=env)
        ready, _, _ = select.select([process.stderr], [], [], 10)
        t.check(ready and process.stderr.readline() == 'ready\n',
                f'worker blocked behind stdout, seed {seed}')
        stdout, stderr = process.communicate(timeout=10)
        t.check(process.returncode == 0 and stdout == 'x' * 100000,
                f'write lost or duplicated, seed {seed}', stderr)


@threads_test('threads output lines')
def output_lines(t):
    for seed in (None, *SEEDS):
        result = t.otium('tests/threads/write-lines.ot',
                         env={'OTIUM_SCHED': seed} if seed else {})
        lines = result.stdout.splitlines()
        t.check(result.status == 0 and len(lines) == 15 and
                all(lines.count(c * 20000) == 5 for c in 'abc'),
                f'lines interleaved, seed {seed}', result.stderr)


@threads_test('threads killed writer releases port')
def killed_writer(t):
    process = t.spawn(t.command('tests/threads/kill-writer.ot'), text=True)
    ready, _, _ = select.select([process.stderr], [], [], 10)
    t.check(ready and process.stderr.readline() == 'killed\n', 'writer did not unwind')
    stdout, stderr = process.communicate(timeout=10)
    t.check(process.returncode == 0 and stdout.endswith('done\n') and
            0 < len(stdout) < 100000, 'port stayed locked or buffered write survived kill', stderr)


threads_test('threads interrupt can wait')(seeded('tests/threads/interrupt-waits.ot'))
threads_test('threads interrupt can wait real')(real('tests/threads/interrupt-waits.ot'))


@threads_test('threads interrupt parked writer')
def interrupt_parked_writer(t):
    process = t.spawn(t.command('tests/threads/interrupt-writer.ot'), text=True)
    ready, _, _ = select.select([process.stderr], [], [], 10)
    t.check(ready and process.stderr.readline() == 'ready\n', 'interrupt did not run')
    stdout, stderr = process.communicate(timeout=10)
    t.check(process.returncode == 0 and stdout == 'x' * 100000 + 'interrupted\ndone\n',
            'interrupt lost or duplicated output', stderr)


@threads_test('threads partial input retries')
def partial_input(t):
    cases = [
        ('char', b'\xce', b'\xbb', 'λ\n'),
        ('chars', b'A\xce', b'\xbb', 'Aλ\n'),
        ('datum', b'("' + b'x' * 5000 + b'\xce', b'\xbb" 42)', '5001 42\n'),
        ('bytes', b'abc', b'de', '5\n#u8(97 98 99 100 101)\n'),
        ('line', b'first\r', b'\n', 'first\n'),
    ]
    for seed in (None, *SEEDS):
        for mode, prefix, suffix, expected in cases:
            env = t.environment({'OTIUM_SCHED': seed})
            process = t.spawn(t.command('tests/threads/partial-input.ot', '--', mode), env=env)
            process.stdin.write(prefix)
            process.stdin.flush()
            ready, _, _ = select.select([process.stderr], [], [], 10)
            t.check(ready and process.stderr.readline() == b'#u8(0 0 0 0 0)\n',
                    f'partial {mode} blocked observer or mutated destination, seed {seed}')
            stdout, stderr = process.communicate(suffix, timeout=10)
            t.check(process.returncode == 0 and stdout.decode() == expected,
                    f'partial {mode} lost input, seed {seed}', stdout, stderr)


@threads_test('threads canceled reader preserves input')
def canceled_reader(t):
    process = t.spawn(t.command('tests/threads/cancel-reader.ot'), text=True)
    process.stdin.write('partial input')
    process.stdin.flush()
    ready, _, _ = select.select([process.stderr], [], [], 10)
    t.check(ready and process.stderr.readline() == 'ready\n', 'reader did not cancel')
    stdout, stderr = process.communicate('\n', timeout=10)
    t.check(process.returncode == 0 and stdout == 'partial input\n',
            'canceled reader consumed partial input', stdout, stderr)


@threads_test('threads input interrupt with sleepers')
def input_interrupt_with_sleepers(t):
    process = t.spawn(t.command('tests/threads/read-interrupt.ot'), text=True)
    ready, _, _ = select.select([process.stdout], [], [], 10)
    t.check(ready and process.stdout.readline() == 'ready\n', 'reader did not start')
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=2)
    t.check(process.returncode == 130 and 'main cleanup\n' in stdout,
            'reader ignored SIGINT or missed cleanup', stdout, stderr)


threads_test('threads fd waits')(seeded('tests/threads/fd-waits.ot'))
threads_test('threads fd waits real')(real('tests/threads/fd-waits.ot'))


@threads_test('threads interrupt cancels background output')
def interrupt_background_output(t):
    process = t.spawn(t.command('tests/threads/interrupt-background-writer.ot'), text=True)
    ready, _, _ = select.select([process.stderr], [], [], 10)
    t.check(ready and process.stderr.readline() == 'ready\n', 'writer did not park')
    process.send_signal(signal.SIGINT)
    process.wait(timeout=2)  # Keep stdout full until the process has exited.
    stdout, stderr = process.communicate()
    t.check(process.returncode == 130 and len(stdout) < 100000,
            'shutdown tried to finish a canceled write', stderr)


threads_test('threads watchdog without spawn')(real('tests/threads/watchdog.ot'))
threads_test('threads watchdog seeded')(seeded('tests/threads/watchdog.ot'))


threads_test('threads interrupt mailbox cursor')(seeded('tests/threads/interrupt-mailbox.ot'))
threads_test('threads interrupt mailbox cursor real')(real('tests/threads/interrupt-mailbox.ot'))


@threads_test('threads watchdog while waiting on fd')
def watchdog_waiting_on_fd(t):
    for seed in (None, *SEEDS):
        process = t.spawn(t.command('tests/threads/read-watchdog.ot'), text=True,
                          env=t.environment({'OTIUM_SCHED': seed}))
        ready, _, _ = select.select([process.stdout], [], [], 2)
        t.check(ready and process.stdout.readline() == 'fired\n',
                f'fd wait ignored watchdog, seed {seed}')
        stdout, stderr = process.communicate('done\n', timeout=2)
        t.check(process.returncode == 0 and stdout == 'checks passed\n',
                'watchdog did not restore input wait', stdout, stderr)


@threads_test('threads formatted output clears retry')
def formatted_output_retry(t):
    for mode in ('display', 'write'):
        for seed in (None, *SEEDS):
            process = t.spawn(t.command('tests/threads/print-output.ot', '--', mode),
                              text=True, env=t.environment({'OTIUM_SCHED': seed}))
            ready, _, _ = select.select([process.stderr], [], [], 10)
            t.check(ready and process.stderr.readline() == 'ready\n',
                    f'{mode} blocked other threads, seed {seed}')
            stdout, stderr = process.communicate(timeout=10)
            expected = 'x' * 100000
            if mode == 'write':
                expected = '"' + expected + '"'
            t.check(process.returncode == 0 and stdout == expected,
                    f'{mode} lost output or retained a wake result, seed {seed}', stderr)


@threads_test('threads atomic section blocks on output')
def atomic_write(t):
    """A full pipe holds the whole process inside an atomic section."""
    process = t.spawn(t.command('tests/threads/atomic-write.ot'), text=True)
    stdout, stderr = process.communicate(timeout=10)
    t.check(process.returncode == 0 and stdout == 'x' * 100000 and stderr == 'atomic\n',
            'another thread ran inside the atomic section', stderr)


@threads_test('threads file lines')
def file_lines(t):
    path = t.scratch / 'lines.txt'
    for seed in (None, *SEEDS):
        result = t.otium('tests/threads/file-lines.ot', '--', path,
                         env={'OTIUM_SCHED': seed} if seed else {})
        t.check(result.status == 0 and result.stdout == '6001\n(#t #t #t)\n',
                f'file lines split or lost, seed {seed}', result.stdout, result.stderr)


@threads_test('threads socket output parks')
def socket_output(t):
    ours, theirs = socket.socketpair()
    process = t.spawn(t.command('tests/threads/write-socket.ot'), text=True,
                      stdout=theirs.fileno())
    theirs.close()
    ready, _, _ = select.select([process.stderr], [], [], 10)
    t.check(ready and process.stderr.readline() == 'ready\n', 'worker blocked behind socket')
    received = bytearray()
    while chunk := ours.recv(65536):
        received += chunk
    ours.close()
    process.wait(timeout=10)
    t.check(process.returncode == 0 and received == b'x' * 4000000, 'socket write lost or duplicated')


@threads_test('threads socket input parks')
def socket_input(t):
    ours, theirs = socket.socketpair()
    process = t.spawn(t.command('tests/threads/read-socket.ot'), text=True,
                      stdin=theirs.fileno())
    theirs.close()
    ours.sendall(b'x' * 5000)
    ready, _, _ = select.select([process.stderr], [], [], 10)
    t.check(ready and process.stderr.readline() == 'ready\n', 'worker blocked behind socket')
    ours.sendall(b'\n')
    ours.close()
    stdout, stderr = process.communicate(timeout=10)
    t.check(process.returncode == 0 and stdout == '5000\n', 'socket input lost', stdout, stderr)
