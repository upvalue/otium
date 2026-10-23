"""C runtime: conformance files, source rules, multimethod oracle, binary CLI and server."""
import json
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import time

from harness import ROOT, Failure, built, case, defaults, test

defaults(suite='c')
OTIUM = built('otium')
FIXTURE = 'c/tests/cli-load.ot'
SECOND = 'c/tests/cli-load-second.ot'

for name in ('01-reader', '02-types', '03-equality', '04-tables', '05-control',
             '06-closures', '07-macros', '08-conditions', '09-params', '11-multimethods'):
    case('conformance ' + name, f'c/tests/otium/{name}.ot', runtimes=['c'],
         stdout_file=f'c/tests/otium/expected/{name}.txt')

case('extension', 'c/tests/ext-demo.ot', runtimes=['c'], paths=['c/tests/modules'])

# Allocation goes through the host allocator; ot-posix.c is the default backend
# and the only file that may name libc allocators. The lookbehind keeps
# ot_alloc/ot_free and prose like "allocation-free (".
SOURCE_RULES = {
    'weak attribute': re.compile(r'__attribute__\s*\(\([^\n]*\bweak\b|\[\[\s*gnu::weak\s*\]\]'),
    'alias attribute': re.compile(r'__attribute__\s*\(\([^\n]*\balias\s*\(|\[\[\s*gnu::alias\b'),
    'raw allocator call': re.compile(r'(?<![\w-])(?:malloc|calloc|realloc|free)\s*\('),
}
EXEMPT = {'raw allocator call': {'src/ot-posix.c'}}


@test('hygiene')
def hygiene(t):
    root = ROOT / 'c'
    failures = []
    paths = sorted(path for directory in ('src', 'tests', '../examples')
                   for path in (root / directory).rglob('*')
                   if path.suffix in {'.c', '.cc', '.cpp', '.h', '.hh', '.hpp'})
    for path in paths:
        relative = os.path.relpath(path, root)
        for number, line in enumerate(path.read_text().splitlines(), 1):
            if relative.startswith('src/'):
                failures += [f'{relative}:{number}: banned {name}'
                             for name, pattern in SOURCE_RULES.items()
                             if relative not in EXEMPT.get(name, ()) and pattern.search(line)]
            elif re.search(r'^\s*#\s*define\s+OT_INTERNAL\b', line):
                failures.append(f'{relative}:{number}: OT_INTERNAL is private to src/')
    headers = sorted(path.name for path in (root / 'src').rglob('*.h'))
    if headers != ['otium.h']:
        failures.append('src must contain exactly one header, src/otium.h; found ' + ', '.join(headers))
    t.check(not failures, '\n'.join(failures))


@test('multimethods', timeout=180)
def multimethods(t):
    """Wildcard resolution against a bitset oracle, then namespace integration."""
    forms = ['(require (otium.generic :as g :refer (Number)))', '(defgeneric (f a b c))']
    expected = []
    for subset in range(256):
        masks = [m for m in range(8) if subset & (1 << m)]
        best = [m for m in masks if not any(m != n and (m & n) == m for n in masks)]
        answer = 'missing-method' if not best else 'ambiguous-method' if len(best) > 1 else str(best[0])
        for order in (masks, list(reversed(masks))):
            forms.append('(reset-generic! (f a b c))')
            for mask in order:
                parameters = ' '.join(f'({p} Number)' if mask & (1 << i) else p for i, p in enumerate('abc'))
                forms.append(f'(defmethod (f {parameters}) {mask})')
            forms.append('(println (try (f 1 2 3) (catch (error? e) (get e :code))))')
            expected.append(answer)
    forms.append((ROOT / 'c/tests/multimethods/namespaces.ot').read_text())
    expected += ['#f', 'ambiguous-method', 'exact', '#t', '#t', 'exact', 'binding-conflict',
                 'binding-conflict', '#t', 'ambiguous-method', '#t', 'number', '#f',
                 'wrong-arity', 'number']
    program = t.scratch / 'checks.ot'
    program.write_text('\n'.join(forms))
    t.expect(t.run([OTIUM, '--no-project', '--path', 'c/tests/multimethods', program], timeout=180),
             stdout=''.join(line + '\n' for line in expected))


def expect(t, result, status, stdout=None, stderr=None):
    return t.expect(result, status=status, contains=[stdout] if stdout else [],
                    stderr=[stderr] if stderr else [])


@test('c cli')
def cli(t):
    def run(*args, stdin=''):
        return t.run([OTIUM, *args], stdin=stdin)
    for text in ('--repl', '--c.server', '--c.heap-max', '--c.gc-stats'):
        expect(t, run('--help'), 0, text)
    expect(t, run(), 0, 'otium repl')
    t.expect(run(FIXTURE), stdout='loaded-from-file\n')
    expect(t, run(FIXTURE, '--repl'), 0, 'loaded-from-file\notium repl')
    expect(t, run('--repl', FIXTURE), 0, 'loaded-from-file\notium repl')
    t.expect(run('--load', FIXTURE, SECOND), stdout='loaded-from-file\nloaded-from-second-file\n')
    expect(t, run('--repl', '--load', FIXTURE, SECOND), 0,
           'loaded-from-file\nloaded-from-second-file\notium repl')
    t.expect(run('c/tests/misplaced-shebang.ot'), status=1, stderr=['reserved # token'])
    script = t.scratch / 'script'
    script.write_text(f'#!{OTIUM} --no-project\n(println "shebang-ok")\n')
    script.chmod(0o755)
    t.expect(t.run([script]), status=1, stderr=['reserved # token'])
    expect(t, run('--unknown'), 2, stderr='unknown option --unknown')
    expect(t, run('--path'), 2, stderr='--path requires a directory')
    expect(t, run('--repl', '--c.server'), 2, stderr='cannot be used together')
    expect(t, run('--c.max-depth'), 2, stderr='--c.max-depth requires =INTEGER')
    expect(t, run('--c.max-depth=nope'), 2, stderr='requires an integer')
    expect(t, run('--c.heap-init=512'), 2, stderr='out of range')
    expect(t, run('--c.heap-max=1024'), 2, stderr='must be at least --c.heap-init')
    t.expect(run('--c.max-depth=1024', f'--c.heap-init={1 << 20}', f'--c.heap-max={8 << 20}', FIXTURE),
             stdout='loaded-from-file\n')


@test('c gc stats')
def gc_stats(t):
    stats = t.expect(t.run([OTIUM, '--c.gc-stats', FIXTURE]), stdout='loaded-from-file\n',
                     stderr=['GC stats:\n']).stderr
    for field in ('allocations', 'collections'):
        t.check(re.search(rf'^  {field}: \d+$', stats, re.M), 'missing', field, stats)
    for field in ('allocated bytes', 'copied bytes', 'reclaimed bytes', 'heap used bytes',
                  'peak heap used bytes', 'heap capacity bytes'):
        t.check(re.search(rf'^  {field}: \d+ \(\d+(?:\.\d{{2}})? (?:B|KiB|MiB|GiB|TiB|PiB|EiB)\)$',
                          stats, re.M), 'missing', field, stats)
    capacity = re.search(r'^  heap capacity bytes: (\d+) ', stats, re.M)
    t.check(capacity and int(capacity[1]) > 0, 'no heap capacity', stats)
    result = t.expect(t.run([OTIUM, '--c.gc-stats-json', '--c.gc-force-compact', FIXTURE]),
                      stdout='loaded-from-file\n', stderr=['OTIUM_GC_STATS '])
    line = next(line for line in result.stderr.splitlines() if line.startswith('OTIUM_GC_STATS '))
    fields = json.loads(line.removeprefix('OTIUM_GC_STATS '))
    expected = set('''allocations collections allocated_bytes copied_bytes promoted_bytes moved_bytes
        reclaimed_bytes mark_stack_overflows used_bytes peak_used_bytes capacity_bytes reserved_bytes
        metadata_bytes fragmentation_bytes largest_free_region_bytes full_copy_collections
        full_copy_total_pause_ns full_copy_max_pause_ns minor_collections minor_total_pause_ns
        minor_max_pause_ns major_sweep_collections major_sweep_total_pause_ns
        major_sweep_max_pause_ns major_compact_collections major_compact_total_pause_ns
        major_compact_max_pause_ns mutator_pause_collections mutator_pause_total_ns
        mutator_pause_max_ns'''.split())
    t.check(set(fields) == expected, 'GC stats fields differ', sorted(set(fields) ^ expected))
    t.check(all(isinstance(value, int) and value >= 0 for value in fields.values()), fields)


@test('c repl')
def repl(t):
    def run(stdin):
        return t.run([OTIUM], stdin=stdin)
    result = t.expect(run('(+ 1\n 2)\nnil\n(quit)\n(println "after-quit")\n'),
                      contains=['3\n', 'nil\n'], absent=['after-quit'])
    t.check(result.stderr == '', result)
    result = t.expect(run('(exit)\n(println "after-exit")\n'), absent=['after-exit'])
    t.check(result.stderr == '', result)
    expect(t, run('(unwind-protect (exit 7) (println "exit-cleanup"))\n'), 7, 'exit-cleanup\n')
    expect(t, run('(println (try (exit 256) (catch (error? e) (condition-message e))))\n(quit)\n'), 0,
           '`exit` expected status from 0 to 255\n')
    expect(t, run('(def hits (array)) (push! hits 1) (+ 1\n 2) (println "hits:" (length hits))\n(quit)\n'),
           0, 'hits: 1\n')
    expect(t, run('(println "a\n   \nb")\n(quit)\n'), 0, 'a\n   \nb\n')
    result = t.expect(run('(+ 1\n'))
    t.check('read error' not in result.stderr, 'incomplete input reported a read error', result)


def frame(source):
    return source + '\n\x1f\n'


@test('c server')
def server(t):
    result = t.run([OTIUM, '--c.server'], stdin=frame('(+ 1 2)') +
                   frame('(restart-case (error "server") (use-value (v) v))') + frame('(+ 3 4)'))
    t.expect(result, contains=['3\n\x1eot> ', 'error:', '7\n\x1eot> '], absent=['restart #?'])
    t.check(result.stdout.count('\x1eot> ') == 3 and result.stderr == '', result)


@test('c server interrupt')
def server_interrupt(t):
    process = t.spawn([OTIUM, '--no-project', '--c.server'])
    # A deadline, not a latency bound: the server shares the machine with the
    # other tests, and collecting at every allocation is much slower.
    limit = 30 if t.stress else 10

    def send(source):
        process.stdin.write(frame(source).encode())
        process.stdin.flush()

    def response():
        output = b''
        deadline = time.monotonic() + limit
        while b'\x1eot> ' not in output:
            ready, _, _ = select.select([process.stdout], [], [], max(0, deadline - time.monotonic()))
            if not ready:
                raise Failure(f'server response timed out: {output!r}')
            chunk = os.read(process.stdout.fileno(), 4096)
            if not chunk:
                raise Failure(f'server closed its output: {output!r}')
            output += chunk
        return output.decode()

    def expect_response(text):
        received = response()
        t.check(text in received, f'expected {text!r}, got {received!r}')
        return received

    send('(+ 1 1)')
    expect_response('2\n\x1eot> ')
    send('(begin (def counter 0) (def cleaned #f) '
         '(unwind-protect (while #t (set! counter (+ counter 1))) (set! cleaned #t)))')
    time.sleep(0.05)
    process.send_signal(signal.SIGINT)
    expect_response('paused\n\x1eot> ')
    send("(not (nil? (find-restart 'continue)))")
    expect_response('#t\n\x1eot> ')
    send("(invoke-restart 'continue)")
    time.sleep(0.05)
    t.check(process.poll() is None, 'server exited after continue')
    process.send_signal(signal.SIGINT)
    expect_response('paused\n\x1eot> ')
    send("(invoke-restart 'abort)")
    expect_response('interrupted\n\x1eot> ')
    send('cleaned')
    expect_response('#t\n\x1eot> ')
    send('counter')
    counter = response()
    t.check(int(counter.splitlines()[0]) > 0, counter)
    send('(while #t nil)')
    time.sleep(0.05)
    process.send_signal(signal.SIGINT)
    expect_response('paused\n\x1eot> ')
    process.send_signal(signal.SIGINT)
    expect_response('interrupted\n\x1eot> ')
    send('(+ 20 22)')
    expect_response('42\n\x1eot> ')
    send('(quit)')
    remaining, errors = process.communicate(timeout=2)
    t.check((process.returncode, remaining, errors) == (0, b'', b''), process.returncode, remaining, errors)
