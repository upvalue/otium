"""Test definitions and the helpers they share.

Definition files are named tests.py and live beside their fixtures. They
register tests with `case` (run one Otium program and compare its result) or
`test` (a function for anything else). tests/run lists and runs them; meson
registers one test per name.
"""
import difflib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time

from otium_tools import BUILD, LIBRARY_EXTENSION, ROOT, command, environment

STRESS = bool(os.environ.get('OTIUM_GC_STRESS'))
TESTS = {}
_defaults = {}


class Failure(Exception):
    pass


class Skip(Exception):
    pass


class Result:
    def __init__(self, command, status, stdout, stderr):
        self.command, self.status, self.stdout, self.stderr = command, status, stdout, stderr

    def __str__(self):
        return (f'$ {shlex.join(map(str, self.command))}\nexit {self.status}\n'
                f'--- stdout\n{self.stdout}--- stderr\n{self.stderr}')


class Test:
    def __init__(self, name, function, suite, timeout, needs, parallel, source):
        self.name, self.function = name, function
        self.suite, self.timeout, self.needs = suite, timeout, set(needs)
        self.parallel, self.source = parallel, source


def defaults(**options):
    """Set suite, needs, timeout or parallel for the rest of this definition file."""
    _defaults.update(options)


def test(name, *, suite=None, timeout=None, needs=None, parallel=None):
    """Register function(t) as the test called name."""
    def register(function):
        if name in TESTS:
            raise ValueError(f'duplicate test name: {name}')
        TESTS[name] = Test(name, function, suite or _defaults.get('suite', 'lang'),
                           timeout or _defaults.get('timeout', 60),
                           _defaults.get('needs', ()) if needs is None else needs,
                           _defaults.get('parallel', True) if parallel is None else parallel,
                           _defaults.get('source'))
        return function
    return register


def case(name, program, *, options=(), paths=(), arguments=(), stdin='', status=0,
         stdout=None, stdout_file=None, contains=(), stderr=(), env=None, **registration):
    """Run program and compare its exit status and output.

    paths become -L options and arguments follow --. stdout is exact; contains
    and stderr list substrings. status=None accepts any nonzero exit."""
    def run(t):
        expected = (ROOT / stdout_file).read_text() if stdout_file else stdout
        flags = [part for path in paths for part in ('-L', path)]
        command = [*options, *flags, program, *(['--', *arguments] if arguments else [])]
        t.expect(t.otium(*command, stdin=stdin, env=env), status=status, stdout=expected,
                 contains=contains, stderr=stderr)
    test(name, **registration)(run)


class Context:
    """What a test function receives: a scratch directory and process helpers."""

    def __init__(self):
        self.stress = STRESS
        self._scratch = None
        self._processes = []
        self._cleanups = []

    @property
    def scratch(self):
        if self._scratch is None:
            self._scratch = tempfile.TemporaryDirectory(prefix='otium-test-')
        return Path(self._scratch.name)

    def defer(self, function):
        """Call function when the test ends, before its processes are killed."""
        self._cleanups.append(function)

    def close(self):
        for function in reversed(self._cleanups):
            function()
        for process in self._processes:
            if process.poll() is None:
                process.kill()
                process.communicate()
        if self._scratch is not None:
            self._scratch.cleanup()

    def environment(self, overrides=None):
        """os.environ with overrides; an override of None removes the variable."""
        env = environment(overrides=overrides)
        return {key: value for key, value in env.items() if value is not None}

    def command(self, *arguments, project=False):
        """A launcher command, isolated from project.ot unless project."""
        return command(*arguments, isolated=not project)

    def run(self, command, *, stdin='', env=None, cwd=ROOT, timeout=60):
        command = list(map(str, command))
        try:
            result = subprocess.run(command, input=stdin, text=True, capture_output=True,
                                    cwd=cwd, env=self.environment(env), timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise Failure(f'$ {shlex.join(command)}\ntimed out after {timeout}s\n'
                          f'--- stdout\n{error.stdout or ""}\n--- stderr\n{error.stderr or ""}')
        return Result(command, result.returncode, result.stdout, result.stderr)

    def otium(self, *arguments, project=False, **options):
        return self.run(self.command(*arguments, project=project), **options)

    def spawn(self, command, *, env=None, cwd=ROOT, **options):
        """Start a process that is killed when the test ends."""
        options.setdefault('stdin', subprocess.PIPE)
        options.setdefault('stdout', subprocess.PIPE)
        options.setdefault('stderr', subprocess.PIPE)
        process = subprocess.Popen(list(map(str, command)), cwd=cwd,
                                   env=self.environment(env), **options)
        self._processes.append(process)
        return process

    def expect(self, result, *, status=0, stdout=None, contains=(), absent=(), stderr=()):
        """Fail unless result matches. status=None accepts any nonzero exit."""
        problems = []
        if status is None and result.status == 0:
            problems.append('expected a nonzero exit')
        elif status is not None and result.status != status:
            problems.append(f'expected exit {status}')
        if stdout is not None and result.stdout != stdout:
            diff = difflib.unified_diff(stdout.splitlines(True), result.stdout.splitlines(True),
                                        'expected', 'actual')
            problems.append('stdout differs:\n' + ''.join(diff))
        problems += [f'stdout lacks {text!r}' for text in contains if text not in result.stdout]
        problems += [f'stdout has {text!r}' for text in absent if text in result.stdout]
        problems += [f'stderr lacks {text!r}' for text in stderr if text not in result.stderr]
        if problems:
            raise Failure('\n'.join(problems) + '\n' + str(result))
        return result

    def check(self, condition, *message):
        if not condition:
            raise Failure(' '.join(map(str, message)) or 'check failed')

    def check_in(self, part, text):
        """Fail unless part occurs in text; return text."""
        if part not in text:
            raise Failure(f'expected {part!r} in {text!r}')
        return text

    def wait(self, predicate, *, timeout=10, interval=0.025, process=None, what='condition'):
        """Poll predicate until it returns a true value, failing if process exits first."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            if process is not None and process.poll() is not None:
                out, err = process.communicate()
                raise Failure(f'process exited {process.returncode} before {what}\n'
                              f'--- stdout\n{out}\n--- stderr\n{err}')
            time.sleep(interval)
        raise Failure(f'timed out waiting for {what}')


def built(name):
    """A path in the Meson build directory."""
    return BUILD / name


def library(name):
    """A shared library or module built by Meson."""
    return BUILD / f'{name}.{LIBRARY_EXTENSION}'
