"""C runtime: conformance files, multimethod oracle, binary CLI, server and embedding host."""
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
    case('conformance ' + name, f'c/tests/otium/{name}.ot',
         stdout_file=f'c/tests/otium/expected/{name}.txt')


@test('extension', needs=['c-host'])
def extension(t):
    """Native procedures and host objects from an embedding host's demo namespace."""
    host = built('c/otium-extension-demo')
    t.expect(t.run([host, '-L', 'c/tests/modules', 'c/tests/ext-demo.ot']))


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
            forms.append('(println (try (f 1 2 3) (catch (error? e) (get (get e :data) 0))))')
            expected.append(answer)
    forms.append((ROOT / 'c/tests/multimethods/namespaces.ot').read_text())
    expected += ['#f', 'ambiguous-method', 'exact', '#t', '#t', 'exact', 'binding-conflict',
                 'binding-conflict', '#t', 'ambiguous-method', '#t', 'number', '#f',
                 'wrong-arity', 'number']
    program = t.scratch / 'checks.ot'
    program.write_text('\n'.join(forms))
    t.expect(t.otium('--path', 'c/tests/multimethods', program, timeout=180),
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
    # The REPL prints a banner and prompts only on a terminal, so these check
    # that it evaluates input.
    t.expect(run(stdin='(+ 40 2)\n'), stdout='42\n')
    t.expect(run(FIXTURE), stdout='loaded-from-file\n')
    t.expect(run(FIXTURE, '--repl', stdin='(+ 40 2)\n'), stdout='loaded-from-file\n42\n')
    t.expect(run('--repl', FIXTURE, stdin='(+ 40 2)\n'), stdout='loaded-from-file\n42\n')
    t.expect(run('--load', FIXTURE, SECOND), stdout='loaded-from-file\nloaded-from-second-file\n')
    t.expect(run('--repl', '--load', FIXTURE, SECOND, stdin='(+ 40 2)\n'),
             stdout='loaded-from-file\nloaded-from-second-file\n42\n')
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
    t.check(re.search(r'^  collections: \d+$', stats, re.M), 'missing', 'collections', stats)
    for field in ('allocated bytes', 'copied bytes', 'reclaimed bytes', 'heap used bytes',
                  'peak heap used bytes', 'heap capacity bytes'):
        t.check(re.search(rf'^  {field}: \d+ \(\d+(?:\.\d{{2}})? (?:B|KiB|MiB|GiB|TiB|PiB|EiB)\)$',
                          stats, re.M), 'missing', field, stats)
    capacity = re.search(r'^  heap capacity bytes: (\d+) ', stats, re.M)
    t.check(capacity and int(capacity[1]) > 0, 'no heap capacity', stats)
    result = t.expect(t.run([OTIUM, '--c.gc-stats-json', FIXTURE]),
                      stdout='loaded-from-file\n', stderr=['OTIUM_GC_STATS '])
    line = next(line for line in result.stderr.splitlines() if line.startswith('OTIUM_GC_STATS '))
    fields = json.loads(line.removeprefix('OTIUM_GC_STATS '))
    # The collector copies between two semispaces; capacity and limit are per semispace.
    expected = set('''collections allocated_bytes copied_bytes reclaimed_bytes used_bytes
        peak_used_bytes capacity_bytes limit_bytes'''.split())
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
           'exit: expected integer status from 0 to 255\n')
    expect(t, run('(def hits (array)) (push! hits 1) (+ 1\n 2) (println "hits:" (length hits))\n(quit)\n'),
           0, 'hits: 1\n')
    expect(t, run('(println "a\n   \nb")\n(quit)\n'), 0, 'a\n   \nb\n')
    # Input ending inside a form fails, as in c cli preloads and arguments, but
    # names the problem rather than a generic read error.
    result = t.expect(run('(+ 1\n'), status=1)
    t.check('read error' not in result.stderr, 'incomplete input reported a read error', result)


def frame(source):
    return source + '\n\x1f\n'


@test('c server')
def server(t):
    result = t.run([OTIUM, '--c.server'], stdin=frame('(+ 1 2)') +
                   frame('(restart-case (error "server") (use-value (v) v))') + frame('(+ 3 4)'))
    t.expect(result, contains=['3\n\x1eot> ', 'error:', '7\n\x1eot> '], absent=['restart #?'])
    t.check(result.stdout.count('\x1eot> ') == 3 and result.stderr == '', result)
    source = '(length "' + 'x' * 8192 + '")'
    result = t.run([OTIUM, '--c.server'], stdin=frame(source) + '(+ 20\n 22)\n\x1f')
    t.expect(result, stdout='8192\n\x1eot> 42\n\x1eot> ', stderr='')


@test('c server interrupt')
def server_interrupt(t):
    process = t.spawn([OTIUM, '--no-project', '--c.server'])
    # A deadline, not a latency bound: the server shares the machine with the
    # other tests, and collecting at every allocation is much slower.
    limit = 30 if t.stress else 10
    running = t.scratch / 'running'
    marker = json.dumps(str(running))

    def wait_running():
        deadline = time.monotonic() + limit
        while not running.exists():
            t.check(process.poll() is None, 'server exited before entering the loop')
            t.check(time.monotonic() < deadline, 'server did not enter the loop')
            time.sleep(0.001)
        running.unlink()

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
    send('(begin (def counter 0) (def cleaned #f) (def announce #t) '
         '(unwind-protect (while #t (set! counter (+ counter 1)) '
         f'(when announce (set! announce #f) (write-file {marker} "ready"))) '
         '(set! cleaned #t)))')
    wait_running()
    process.send_signal(signal.SIGINT)
    expect_response('paused\n\x1eot> ')
    send("(not (nil? (find-restart 'continue)))")
    expect_response('#t\n\x1eot> ')
    send('(begin (set! announce #t) #t)')
    expect_response('#t\n\x1eot> ')
    send("(invoke-restart 'continue)")
    wait_running()
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
    # Send the second interrupt as soon as the paused prompt is visible. The
    # server may not yet have entered its input wait when this signal arrives.
    for _ in range(20):
        send(f'(begin (write-file {marker} "ready") (while #t nil))')
        wait_running()
        process.send_signal(signal.SIGINT)
        expect_response('paused\n\x1eot> ')
        process.send_signal(signal.SIGINT)
        expect_response('interrupted\n\x1eot> ')
    send('(+ 20 22)')
    expect_response('42\n\x1eot> ')
    send('(quit)')
    remaining, errors = process.communicate(timeout=2)
    t.check((process.returncode, remaining, errors) == (0, b'', b''), process.returncode, remaining, errors)


def cli(t, *args, stdin='', status=0, stdout=None, **options):
    result = t.run([OTIUM, '--no-project', *map(str, args)], stdin=stdin, **options)
    t.expect(result, status=status, stdout=stdout)
    return result


@test('c cli preloads and arguments')
def cli_preloads(t):
    cli(t, '--repl', stdin='nil\n(println\n [nil :repl])\n', stdout='nil\n[nil :repl]\nnil\n')
    cli(t, stdin='(+ 1 2) (+ 3 4)\n', stdout='3\n7\n')
    cli(t, '--repl', stdin='(def n 0) (begin\n (set! n (+ n 1))\n n)\nn\n', stdout='0\n1\n1\n')
    cli(t, '--repl', stdin="'\n(a .\n b\n)\n", stdout='(a . b)\n')
    cli(t, '--repl', stdin='(str "a\nb")\n', stdout='a\nb\n')
    cli(t, '--repl', stdin='; comment only\n\n', stdout='')
    cli(t, '--repl', stdin='(+ 1\n', status=1)
    cli(t, '--repl', stdin='"unfinished\n', status=1)
    result = cli(t, '--repl', stdin=')\n(/ 1 0)\n42\n', stdout='42\n')
    t.check('unexpected closing' in result.stderr and 'zero' in result.stderr, result)
    cli(t, '--repl', stdin='(def n 0)\n(set! n 1) (+\n 2 3)\nn\n', stdout='0\nnil\n5\n1\n')
    result = cli(t, '--repl', stdin="'(a . b c)\n42\n", stdout='42\n')
    t.check('expected closing delimiter' in result.stderr, result)
    cli(t, '--repl', stdin='(read-line)\nhello\n(+ 1 2)\n', stdout='hello\n3\n')
    cli(t, '--repl', stdin='(println :a) (println :b)\n', stdout=':a\nnil\n:b\nnil\n')
    cli(t, '-e', '(exit)', stdout='')
    cli(t, '--repl', stdin='(exit 7)\n(println :wrong)\n', status=7, stdout='')
    cli(t, '--load', status=2)
    first, second, script = (t.scratch / name for name in ('first.ot', 'second.ot', 'script.ot'))
    first.write_text('(def order [:first]) (def answer 40)')
    second.write_text('(push! order :second) (set! answer (+ answer 2))')
    script.write_text('(println order answer)')
    cli(t, '--load', first, '--load', second, script, stdout='[:first :second] 42\n')
    cli(t, '--load', first, '--load', second, '-e', 'answer', stdout='42\n')
    cli(t, '--load', first, '--repl', script, stdin='answer\n', stdout='[:first] 40\n40\n')
    cli(t, '--load=' + str(first), '-e', '(+ answer 2)', stdout='42\n')
    cli(t, '--load', first, stdin='(println :unwanted-repl)\n', stdout='')
    first.write_text('(error "preload failure")')
    result = cli(t, '--load', first, script, status=1, stdout='')
    t.check(str(first) in result.stderr and 'preload failure' in result.stderr, result)
    cli(t, '--load', t.scratch / 'missing', script, status=1)
    first.write_text('(exit 7)')
    cli(t, '--load', first, script, status=7, stdout='')
    for text in ('', ' ' * 4096):
        first.write_text(text)
        cli(t, '--load', first, '-e', '42', stdout='42\n')
    fixture = ROOT / 'tests/cli/arguments.ot'
    tokens = ['--help', '', 'two words', 'λ', '--', '-file', '--c.heap-max=1']
    expected = ''.join(f'{len(token)}:{token}\n' for token in tokens) + str(fixture) + '\n'
    cli(t, fixture, '--', *tokens, stdout=expected)
    cli(t, '--', fixture, *tokens, stdout=expected)
    cli(t, '-e', '(otium.process/arguments)', '--', 'λ', '', stdout='("λ" "")\n')
    cli(t, '-e', '(otium.process/script-name)', stdout='nil\n')
    first.write_text('(println (otium.process/arguments))')
    cli(t, '--load', first, fixture, '--', *tokens,
        stdout='("--help" "" "two words" "λ" "--" "-file" "--c.heap-max=1")\n' + expected)


@test('c cli heap limits')
def cli_heap(t):
    large = '(bytevector-length (make-bytevector 2097152))'
    cli(t, '-e', large, stdout='2097152\n')
    cli(t, '--c.heap-max=1048576', '-e', large, status=1)
    cli(t, '--c.heap-init=1048576', '--c.heap-max=4194304', '-e', large, stdout='2097152\n')
    cli(t, '--c.heap-init=2097152', '--c.heap-max=1048576', '-e', '1', status=2)
    cli(t, '--c.heap-init=1023', '-e', '1', status=2)


@test('c cli projects')
def cli_projects(t):
    """Projects are data. Only top-level paths forms contribute search directories."""
    lib = t.scratch / 'module space'
    lib.mkdir()
    (lib / 'sample.ot').write_text('(ns sample) (def value 42)')
    project = t.scratch / 'project.ot'
    project.write_text('; "ignored"\n(paths "module space")\n(lint :allow (if-chain))\n')
    expression = '(require sample :as s) s/value'

    def run(*args, status=0, stdout=None):
        t.expect(t.run([OTIUM, *map(str, args)]), status=status, stdout=stdout)
    run('--project', project, '-e', expression, stdout='42\n')
    run('--project=' + str(project), '-e', expression, stdout='42\n')
    run('--no-project', '--path=' + str(lib), '-e', expression, stdout='42\n')
    for text in ('(paths "module space" . bad)', '(paths "module space" 3)', '(paths "module space"'):
        project.write_text(text)
        run('--project', project, '-e', '1', status=1)
    run('--project', t.scratch / 'missing.ot', '-e', '1', status=1)
    run('--project', project, '--no-project', '-e', '1', status=2)


INTERRUPT_SETUP = """(def (spin) (spin))
(def (double s n) (if (= n 0) s (double (string-append s s) (- n 1))))
(def (ready) (display (double "x" 15)))
"""


@test('c interrupt cleanup')
def interrupt_cleanup(t):
    """SIGINT arrives only after protected code has written its readiness marker."""
    def interrupt(args, expected, status):
        process = t.spawn([OTIUM, '--no-project', *map(str, args)])
        ready, _, _ = select.select([process.stdout], [], [], 15)
        t.check(ready, 'protected body did not become ready')
        first = os.read(process.stdout.fileno(), 1)
        t.check(first == b'x', first, process.poll())
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=15)
        output = first + stdout
        t.check(output.endswith(expected), output[-250:], stderr)
        t.check(set(output[:-len(expected)]) == {ord('x')}, output[-250:])
        t.check(process.returncode == status and not stderr, process.returncode, stderr)
    interrupt(['--load', 'tests/conditions/support.ot', 'tests/conditions/interrupt.ot'],
              b'condition checks: 9\n', 130)
    path = t.scratch / 'interrupt.ot'
    for source, expected, status in [
        ('(unwind-protect (unwind-protect (begin (ready) (spin)) (println :inner)) (println :outer))',
         b':inner\n:outer\n', 130),
        ('(call-with-cleanup (fn () (ready) (spin)) (fn (outcome) (println outcome)))', b':quit\n', 130),
        ('(try (unwind-protect (begin (ready) (spin)) (error :replacement)) (catch e (println e)))',
         b':replacement\n', 0),
        ('(try (call-with-cleanup (fn () (ready) (spin)) (fn (o) (println o) (error :replacement)))'
         ' (catch e (println e)))', b':quit\n:replacement\n', 0),
        ("(restart-case (unwind-protect (begin (ready) (spin)) (invoke-restart 'done))"
         ' (done () (println :recovered)))', b':recovered\n', 0),
        ("(unwind-protect (eval '(begin (ready) (spin))) (println :eval-cleanup))",
         b':eval-cleanup\n', 130),
    ]:
        path.write_text(INTERRUPT_SETUP + source)
        interrupt([path], expected, status)


@test('c standard streams')
def standard_streams(t):
    """Standard stream lookahead, raw output and interrupting a blocking read."""
    path = t.scratch / 'ports.ot'

    def run(stdin=b''):
        return subprocess.run([OTIUM, '--no-project', path], input=stdin, capture_output=True,
                              timeout=10)
    path.write_text('(println (list (peek-char) (read-u8) (read-u8) (read-line) (read-all)))')
    result = run('é🙂\nlast'.encode())
    t.check(result.returncode == 0 and result.stdout.decode() == '(#\\é 195 169 "🙂" "last")\n',
            result)
    path.write_text('(write-u8 255) (write-bytevector #u8(0 1))')
    result = run()
    t.check(result.returncode == 0 and result.stdout == bytes([255, 0, 1]), result)
    path.write_text('(println (read)) (println (read-u8))')
    for source, expected, next_byte in [(b'(a)x', b'(a)\n', 120), (b'"s"x', b's\n', 120),
                                        (b"'a x", b'(quote a)\n', 32), (b'#\\)x', b')\n', 120),
                                        (b'#u8(1)x', b'(bytevector 1)\n', 120),
                                        (b'[1]x', b'(array 1)\n', 120)]:
        process = t.spawn([OTIUM, '--no-project', path])
        process.stdin.write(source)
        process.stdin.flush()
        ready, _, _ = select.select([process.stdout], [], [], 10)
        t.check(ready, 'datum reader waited for EOF')
        first = process.stdout.readline()
        t.check(first == expected, source, first)
        # Leave stdin open until the process exits: the sentinel must suffice.
        process.wait(timeout=10)
        rest = process.stdout.read()
        t.check(rest == f'{next_byte}\n'.encode() and process.returncode == 0, source, rest)
        t.check(not process.stderr.read(), source)
        process.stdin.close()
    path.write_text('(unwind-protect (begin (println :ready) (read-u8)) (println :closed))')
    process = t.spawn([OTIUM, '--no-project', path])
    ready, _, _ = select.select([process.stdout], [], [], 10)
    t.check(ready and process.stdout.readline() == b':ready\n', 'no ready line')
    process.send_signal(signal.SIGINT)
    stdout, stderr = process.communicate(timeout=10)
    t.check(process.returncode == 130 and stdout == b':closed\n' and not stderr,
            process.returncode, stdout, stderr)


SHUTDOWN_SETUP = """(def original-output (current-output-port))
(def sink (make-output-port "sink"
  {:write (fn (bytes start end)
            (write-bytevector bytes original-output start end)
            (- end start))}))
(write-string "buffered" sink)
"""


@test('c shutdown flush')
def shutdown_flush(t):
    """Buffered custom output is flushed at script completion, exit and quit."""
    path = t.scratch / 'shutdown.ot'

    def run(source):
        path.write_text(source)
        return subprocess.run([OTIUM, '--no-project', path], capture_output=True, timeout=10)
    for suffix, status in [('', 0), ('(exit 7)', 7), ('(quit 9)', 9)]:
        result = run(SHUTDOWN_SETUP + suffix)
        t.check(result.returncode == status and result.stdout == b'buffered' and not result.stderr,
                result)
    result = run('(def p (make-output-port "fail" {:write (fn (b s e) (error "write failed"))}))'
                 ' (write-u8 1 p) (exit 7)')
    t.check(result.returncode == 1 and b'write failed' in result.stderr, result)
    result = run('(def p (make-output-port "exit in flush" {:flush (fn () (exit 12))'
                 ' :write (fn (b s e) (- e s))}))')
    t.check(result.returncode == 12 and not result.stderr, result)


@test('c foreign host', needs=['c-host'])
def foreign_host(t):
    """Callback abort boundaries and contained exit, through an embedding host."""
    host = built('c/otium-foreign-control')
    for mode, message in [('outside', 'outside an Otium foreign call'),
                          ('thread', 'thread other than the Otium thread')]:
        result = t.run([host, mode])
        t.check(result.status == -signal.SIGABRT and message in result.stderr, result)
        t.check('AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr,
                result)
    t.expect(t.run([host, 'exit']), stdout='callback exit and outer cleanup passed\n')


@test('c amalgamation', needs=['c-host'])
def amalgamation(t):
    """The single-file distribution builds and runs scripts like the CLI."""
    binary = built('c/otium-amalgamation')
    t.expect(t.run([binary, '--no-project', '-e', '(+ 20 22)']), stdout='42\n')
    t.expect(t.run([binary, '--no-project', ROOT / 'tests/cli/arguments.ot', '--', 'a']),
             stdout='1:a\n' + str(ROOT / 'tests/cli/arguments.ot') + '\n')
