"""Green threads and actors. They need native code: on the VM only the error
is checked."""
import os
import select
import signal

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
