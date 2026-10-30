"""The development server: socket protocol, interruption, introspection and clients."""
import json
import os
from pathlib import Path
import re
import select
import signal
import socket
import struct
import subprocess
import tempfile
import time

from harness import ROOT, Failure, Skip, test
from otium_tools import dev


class Client:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        self.socket.connect(str(path))
        self.events = []

    def close(self):
        self.socket.close()

    def send(self, ident, op, **fields):
        text = '{:id %d :op :%s%s}' % (ident, op, ''.join(
            ' :%s %s' % (key.replace('_', '-'), json.dumps(value, ensure_ascii=False))
            for key, value in fields.items()))
        data = text.encode('utf-8')
        self.socket.sendall(struct.pack('!I', len(data)) + data)

    def read(self):
        def exact(size):
            data = b''
            while len(data) < size:
                part = self.socket.recv(size - len(data))
                if not part:
                    raise Failure('server disconnected')
                data += part
            return data
        size, = struct.unpack('!I', exact(4))
        return exact(size).decode('utf-8')

    def response(self, ident):
        while True:
            data = self.read()
            if data.startswith('{:event '):
                self.events.append(data)
            elif re.search(r':id %d(?:\s|})' % ident, data):
                return data
            else:
                raise Failure(f'expected a response to {ident}, got {data!r}')


class Server:
    """otium --dev on a socket in a short /tmp path, since socket paths are length-limited."""

    def __init__(self, t, *arguments, watchdog=2000, stress=False, paths=()):
        self.directory = Path(tempfile.mkdtemp(prefix='otdev-', dir='/tmp'))
        self.path = self.directory / 'dev.sock'
        self.clients = []
        self.process = t.spawn(
            t.command('--dev=' + str(self.path), '--dev-watchdog=' + str(watchdog),
                      *[part for path in paths for part in ('--path', path)], *arguments),
            env={'OTIUM_GC_STRESS': '1'} if stress else None)
        t.defer(self.close)
        t.wait(self.path.exists, process=self.process, what='the development socket')
        self.client = self.connect()

    def connect(self):
        client = Client(self.path)
        self.clients.append(client)
        return client

    def close(self):
        for client in self.clients:
            client.close()
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.communicate()
        for path in self.directory.iterdir():
            path.unlink()
        self.directory.rmdir()


def serve(t, **options):
    return Server(t, 'serve', **options)


def pause_limit(t):
    """How soon an interrupt must pause; collecting at every allocation is much slower."""
    return 2 if t.stress else .1


@test('dev eval')
def evaluation(t):
    server = serve(t)
    c = server.client
    t.check(server.path.stat().st_mode & 0o777 == 0o700, 'socket is not private')
    c.send(1, 'eval', code='(println "héllo") (+ 1 2)')
    result = c.response(1)
    t.check_in(':value "3"', result)
    t.check_in(':output "héllo\\n"', result)
    c.send(2, 'eval', ns='app.state', code='(defonce value 41)')
    t.check_in(':ok #t', c.response(2))
    other = server.connect()
    other.send(3, 'eval', ns='app.state', code='(+ value 1)')
    t.check_in(':value "42"', other.response(3))
    path = server.directory / 'reload.ot'
    path.write_text('(ns app.state)\n(def value 12)\n(error "broken")\n(def value 99)\n')
    c.send(4, 'load', path=str(path))
    failure = c.response(4)
    t.check_in(':message "broken"', failure)
    t.check_in(':line 3', failure)
    t.check_in(str(path), failure)
    other.send(5, 'eval', ns='app.state', code='value')
    t.check_in(':value "12"', other.response(5))
    c.send(6, 'eval', code='(println "before") (error "oops" 1)')
    result = c.response(6)
    t.check_in(':ok #f', result)
    t.check_in(':output "before\\n"', result)
    c.send(7, 'eval', code='(+ 2 3)')
    t.check_in(':value "5"', c.response(7))
    path.write_text('(ns app.state)\n(def value 20)\n(+ value 2)\n')
    c.send(8, 'load', path=str(path))
    t.check_in(':value "22"', c.response(8))
    c.send(9, 'eval', code='(error #f)')
    t.check_in(':ok #f', c.response(9))


@test('dev interrupt')
def interrupt(t):
    server = serve(t, watchdog=0)
    c = server.client
    c.send(1, 'eval', code='(def stop #f) (def cleaned #f) (unwind-protect (while (not stop) nil) (set! cleaned #t)) 99')
    time.sleep(.1)
    start = time.monotonic()
    c.send(2, 'interrupt')
    t.check_in(':event :paused', c.read())
    t.check(time.monotonic() - start < pause_limit(t), 'pause took too long')
    t.check_in(':ok #t', c.response(2))
    c.send(3, 'eval', code='(set! stop #t)')
    t.check_in(':ok #t', c.response(3))
    c.send(4, 'continue')
    t.check_in(':ok #t', c.response(4))
    t.check_in(':value "99"', c.response(1))
    c.send(5, 'eval', code='cleaned')
    t.check_in(':value "#t"', c.response(5))
    c.send(6, 'eval', code='(set! cleaned #f) (unwind-protect (let loop () (cons 1 2) (loop)) (set! cleaned #t))')
    time.sleep(.1)
    start = time.monotonic()
    c.send(7, 'interrupt')
    t.check_in(':event :paused', c.read())
    t.check(time.monotonic() - start < pause_limit(t), 'pause took too long')
    c.response(7)
    # Disconnecting the interrupting client leaves the process paused.
    replacement = server.connect()
    c.close()
    replacement.send(8, 'status')
    t.check_in(':state :paused', replacement.response(8))
    replacement.send(9, 'abort')
    replacement.response(9)
    replacement.send(10, 'eval', code='cleaned')
    t.check_in(':value "#t"', replacement.response(10))


@test('dev watchdog')
def watchdog(t):
    c = serve(t, watchdog=150).client
    c.send(1, 'eval', code='(let loop () (loop))')
    start = time.monotonic()
    t.check_in(':reason :watchdog', c.read())
    t.check(time.monotonic() - start < pause_limit(t) + .2, 'watchdog took too long')
    c.send(2, 'abort')
    c.response(2)
    t.check_in(':ok #f', c.response(1))
    raw = b'{invalid'
    c.socket.sendall(struct.pack('!I', len(raw)) + raw)
    t.check_in(':ok #f', c.read())
    c.send(3, 'status')
    t.check_in(':ok #t', c.response(3))
    c.send(4, 'continue')
    t.check_in(':ok #f', c.response(4))


@test('dev gc stress')
def gc_stress(t):
    if not t.stress:
        raise Skip('run with the stress test setup')
    c = serve(t, stress=True).client
    for i in range(8):
        c.send(i, 'eval', ns='stress.space', code='(defonce x (list 1 2 3)) (println x) (cons x x)')
        result = c.response(i)
        t.check_in(':ok #t', result)
        t.check_in(':output "(1 2 3)\\n"', result)


@test('dev repl idle')
def repl_idle(t):
    server = Server(t)
    c = server.client
    c.send(1, 'eval', code='(+ 20 22)')
    t.check_in(':value "42"', c.response(1))
    server.process.stdin.write(b'(println "background-output")\n')
    server.process.stdin.flush()
    while 'background-output' not in c.read():
        pass


@test('dev nested pause')
def nested_pause(t):
    c = serve(t, watchdog=150).client
    c.send(1, 'eval', code='(let loop () (loop))')
    t.check_in(':event :paused', c.read())
    c.send(2, 'eval', code='(let loop () (loop))')
    t.check_in(':event :paused', c.read())
    c.send(3, 'abort')
    c.response(3)
    t.check_in(':ok #f', c.response(2))
    c.send(4, 'status')
    t.check_in(':state :paused', c.response(4))
    c.send(5, 'abort')
    c.response(5)
    t.check_in(':ok #f', c.response(1))


@test('dev frames')
def frames(t):
    """Fragmented frames are reassembled; malformed or oversized ones drop only their client."""
    server = serve(t)
    c = server.client
    payload = b'{:id 1 :op :status}'
    for byte in struct.pack('!I', len(payload)) + payload:
        c.socket.sendall(bytes([byte]))
    t.check_in(':ok #t', c.response(1))
    for invalid in (b'\xff', b'\xc0\x80', b'\xed\xa0\x80', b'a\0b', b'\xf0\x9f\x98'):
        other = server.connect()
        other.socket.sendall(struct.pack('!I', len(invalid)) + invalid)
        t.check(other.socket.recv(1) == b'', 'invalid frame kept its client', invalid)
    other = server.connect()
    other.socket.sendall(struct.pack('!I', 4 * 1024 * 1024 + 1))
    t.check(other.socket.recv(1) == b'', 'oversized frame kept its client')
    c.send(2, 'status')
    t.check_in(':ok #t', c.response(2))


@test('dev attach and mcp')
def attach_and_mcp(t):
    server = serve(t)
    result = t.expect(t.run([ROOT / 'otium', 'attach', server.path, '--eval', '(+ 20 22)'], timeout=10))
    t.check_in(':value "42"', result.stdout)
    messages = [
        {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2024-11-05'}},
        {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
        {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call',
         'params': {'name': 'eval', 'arguments': {'code': '(+ 3 4)'}}},
    ]
    result = t.expect(t.run([ROOT / 'otium', 'mcp', server.path], timeout=10,
                            stdin=''.join(json.dumps(message) + '\n' for message in messages)))
    responses = [json.loads(line) for line in result.stdout.splitlines()]
    t.check(responses[0]['id'] == 1, responses)
    t.check(len(responses[1]['result']['tools']) == 6, responses[1])
    t.check(responses[2]['id'] == 3, responses[2])
    t.check_in(':value "7"', responses[2]['result']['content'][0]['text'])

    result = t.expect(t.run([ROOT / 'otium', 'attach', server.path], timeout=10,
                            stdin='(+ 20 22)'))
    t.check_in(':value "42"', result.stdout)
    result = t.expect(t.run([ROOT / 'otium', 'mcp', server.path], timeout=10,
                            stdin=json.dumps(messages[1])))
    t.check(json.loads(result.stdout)['id'] == 2, result.stdout)

    code = '(string-length ' + dev.encode('\0\b\f\x1b') + ')'
    # Send literal control characters in a comment as part of the request string.
    code += ' ; \b\f\x1b'
    result = t.expect(t.run([ROOT / 'otium', 'attach', server.path, '--eval', code], timeout=10))
    t.check_in(':value "4"', result.stdout)


@test('dev events')
def events(t):
    server = serve(t, watchdog=150)
    c = server.client
    c.send(1, 'eval', code='(let loop () (loop))')
    t.check_in(':event :paused', c.read())
    c.send(2, 'abort')
    t.check_in(':ok #t', c.response(2))
    c.response(1)
    c.send(3, 'events', since=0)
    result = c.response(3)
    t.check(re.search(r':event :paused .*:seq 1', result), result)
    t.check(re.search(r':event :resumed .*:seq 2', result), result)
    t.check_in(':event 2}', result)
    c.send(4, 'events', since=1)
    t.check(':paused' not in c.response(4), 'events since 1 repeated the pause')
    c.send(5, 'eval', code='"abcdefghij"', max_chars=4)
    result = c.response(5)
    t.check_in(':value "\\"abc"', result)
    t.check_in(':truncated #t', result)
    c.send(6, 'eval', code='(+ 1 2)')
    t.check(':truncated' not in c.response(6), 'short value marked truncated')
    c.send(7, 'eval', code='\n(error "here")', path='/tmp/scratch.ot')
    t.check_in(':source {:path "/tmp/scratch.ot" :line 2}', c.response(7))
    c.send(8, 'status')
    result = c.response(8)
    t.check_in(':pid ', result)
    t.check(':runtime' not in result, 'status still reports a runtime')


@test('dev introspection')
def introspection(t):
    server = serve(t)
    c = server.client
    path = server.directory / 'demo.ot'
    path.write_text('(ns app.demo)\n\n(defonce model {:count 0})\n\n'
                    '(def (greet name)\n  "Say hi."\n  (str "hi " name))\n\n'
                    '(def- (helper & xs) xs)\n\n(defmacro twice (x) (list (quote begin) x x))\n')
    c.send(1, 'load', path=str(path))
    t.check_in(':ok #t', c.response(1))
    c.send(2, 'eval', code='(require otium.dev) (otium.dev/bindings (quote app.demo))')
    result = c.response(2)
    for part in ('{:name greet :kind :function', '{:name helper :kind :function', ':public #f',
                 '{:name twice :kind :macro', '{:name model :kind :value', 'Say hi.'):
        t.check_in(part, result)
    t.check_in(':arity 1 :variadic #f', result.replace('\\', ''))
    c.send(3, 'eval', code='(otium.dev/namespaces)')
    t.check_in('{:name app.demo :file \\"%s\\"}' % path.resolve(), c.response(3))
    c.send(4, 'eval', code='(otium.dev/apropos "gree")')
    t.check_in(':value "(app.demo/greet)"', c.response(4))
    c.send(5, 'eval', code='(otium.dev/source (quote app.demo/greet))')
    t.check_in(':line 5}', c.response(5))
    c.send(6, 'eval', code='(otium.dev/source (quote app.demo/missing))')
    t.check_in(':value "nil"', c.response(6))
    c.send(7, 'eval', code='(otium.dev/stale-files)')
    t.check_in(':value "()"', c.response(7))
    future = time.time() + 60
    os.utime(path, (future, future))
    c.send(8, 'eval', code='(otium.dev/stale-files)')
    t.check_in(str(path.resolve()), c.response(8))


@test('dev introspection policy')
def introspection_policy(t):
    root = t.scratch / "otdev-'policy"
    (root / 'app').mkdir(parents=True)
    path = root / 'app/demo.ot'
    path.write_text('(ns app.demo)\n(def (zero) 0)\n(def (fixed a b) a)\n'
                    '(def (dotted a . rest) rest)\n(def (amp a b & rest) rest)\n(def all (fn args args))\n')
    (root / 'app/bad.ot').write_text('(ns app.bad) (def ok 1)\n')
    c = serve(t, paths=[root]).client
    c.send(1, 'eval', code='(require app.demo)')
    t.check_in(':ok #t', c.response(1))
    c.send(2, 'eval', code="(map (fn (b) (list (get b :name) (get b :arity) (get b :variadic))) (dev/bindings 'app.demo))")
    result = c.response(2)
    t.check_in('((all 0 #t) (amp 2 #t) (dotted 1 #t) (fixed 2 #f) (zero 0 #f))', result)
    t.check(':parameters' not in result, result)
    c.send(3, 'eval', code="(dev/source 'app.demo/fixed)")
    result = c.response(3)
    t.check_in(str(path.resolve()), result)
    t.check_in(':line 3}', result)
    c.send(4, 'eval', code='(dev/stale-files)')
    t.check_in(':value "()"', c.response(4))
    future = time.time() + 60
    os.utime(path, (future, future))
    c.send(5, 'eval', code='(dev/stale-files)')
    t.check_in(str(path.resolve()), c.response(5))
    os.utime(path, (time.time() - 60, time.time() - 60))
    c.send(6, 'eval', code='(require app.demo :reload) (require otium.dev :reload) (dev/stale-files)')
    t.check_in(':value "()"', c.response(6))
    c.send(7, 'eval', code="(dev/source 'app.demo/fixed)")
    t.check_in(str(path.resolve()), c.response(7))
    c.send(8, 'eval', code='(def saved dev/record-load!) (set! dev/record-load! (fn (name path) (error "load hook failed"))) (require app.bad)')
    result = c.response(8)
    t.check_in(':ok #f', result)
    t.check_in('load hook failed', result)
    c.send(9, 'eval', code='(set! dev/record-load! saved) (require app.bad) app.bad/ok')
    t.check_in(':value "1"', c.response(9))


@test('dev socket collision')
def socket_collision(t):
    server = serve(t)
    result = t.otium('--dev=' + str(server.path), 'serve', project=True, timeout=10)
    t.check(result.status != 0, 'a second server took a live socket', result)
    server.client.send(1, 'status')
    t.check_in(':ok #t', server.client.response(1))


# Servers on the default socket directory can't tell their sockets apart, so
# this runs alone.
@test('dev client', parallel=False)
def client(t):
    """tools/otium-dev against a server on the default socket path."""
    directory = t.scratch
    before = set(dev.DIRECTORY.glob('*.sock')) if dev.DIRECTORY.is_dir() else set()
    process = t.spawn(t.command('--dev-watchdog=200', 'serve'), cwd=directory,
                      stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL)

    def started():
        found = set(dev.DIRECTORY.glob('*.sock')) - before if dev.DIRECTORY.is_dir() else set()
        return found.pop() if found else None
    path = t.wait(started, process=process, what='the development socket')

    def run(*args, stdin=''):
        return t.run([ROOT / 'tools/otium-dev', *args], cwd=directory, stdin=stdin, timeout=20)

    t.check(path.parent.stat().st_mode & 0o777 == 0o700, 'socket directory is not private')
    t.check_in(str(directory.resolve()), run('ls').stdout)
    t.check_in('pid: ', t.expect(run('status')).stdout)
    t.expect(run('eval', '(println "out") (+ 1 2)'), stdout='out\n3\n')
    t.expect(run('eval', '-', stdin='(* 6 7)'), stdout='42\n')
    (directory / 'scratch.ot').write_text('(+ 1\n  (error "broken"))\n')
    t.expect(run('eval', '--file', 'scratch.ot'), status=1, stderr=['error: broken', 'scratch.ot:1'])
    (directory / 'demo.ot').write_text('(ns app.demo)\n(def (greet name) (str "hi " name))\n')
    t.expect(run('load', 'demo.ot'), stdout='loaded 2 forms\n')
    t.expect(run('eval', '--ns', 'app.demo', '(greet "you")'), stdout='"hi you"\n')
    t.expect(run('eval', '--max-chars', '3', '"abcdef"'), stdout='"ab\n', stderr=['truncated'])
    t.expect(run('eval', '(let loop () (loop))'), status=4, stderr=['paused (watchdog'])
    t.expect(run('eval', '(+ 2 2)'), stdout='4\n')
    t.expect(run('abort'), stdout='aborted\n')
    t.expect(run('events'), contains=['#1 paused reason=:watchdog', '#2 resumed'])
    t.expect(run('eval', '--timeout', '0.5', '(require otium.dev) (otium.dev/%poll 2000)'), status=3)
    # A dead server's socket is listed as stale and removed.
    process.kill()
    process.wait()
    t.check(path.exists(), 'socket vanished with its server')
    t.check(path not in [found for found, _ in dev.servers()], 'dead server listed')
    t.check(not path.exists(), 'stale socket was not removed')


UI_PROGRAM = '''(require (otium.ui :as ui))
(def (view) (ui/label {} "Initial"))
(defonce window (ui/open {:headless #t :width 160 :height 100}))
(ui/set-view! window (fn () (view)))
(ui/run)
'''


@test('dev ui', suite='gpu', needs=['ui'], timeout=300, parallel=False)
def live_ui(t):
    """Repair a failing, then looping, live UI through its development socket."""
    script = t.scratch / 'ui.ot'
    script.write_text(UI_PROGRAM)
    server = Server(t, script, watchdog=500)
    client = server.client
    client.send(1, 'eval', code='(length (ui/windows))')
    t.check_in(':value "1"', client.response(1))
    client.send(2, 'eval', code='(def (view) (error "frame broke"))')
    client.response(2)
    while True:
        event = client.read()
        if ':event :frame-error' in event:
            t.check_in('frame broke', event)
            break
    client.send(3, 'eval', code='(def (view) (ui/label {} "Repaired"))')
    t.check_in(':ok #t', client.response(3))
    capture = t.scratch / 'fixed.png'
    client.send(4, 'eval', code='(ui/capture! window "%s")' % capture)
    t.check_in(':ok #t', client.response(4))
    t.wait(capture.exists, timeout=5, what='the capture')
    t.check(capture.read_bytes()[:8] == b'\x89PNG\r\n\x1a\n', 'capture is not a PNG')
    client.send(5, 'eval', code='(def (view) (let loop () (loop)))')
    client.response(5)
    while ':event :paused' not in client.read():
        pass
    client.send(6, 'eval', code='(def (view) (ui/label {} "Resumed"))')
    client.response(6)
    client.send(7, 'abort')
    client.response(7)
    client.send(8, 'eval', code='(length (ui/windows))')
    t.check_in(':value "1"', client.response(8))
    client.send(9, 'eval', code='(ui/close! window)')
    client.response(9)
    out, err = server.process.communicate(timeout=5)
    t.check(server.process.returncode == 0, 'UI exited', server.process.returncode, out, err)


@test('dev source forms')
def source_forms(t):
    server = serve(t)
    path = server.directory / 'forms.ot'
    path.write_text('(ns app.forms (:require (otium.extra :as extra)))\n'
                    '; (def fake 1)\n'
                    '(defparam setting 1)\n'
                    '(defabstract Animal)\n'
                    '(defgeneric (speak animal))\n')
    c = server.client
    c.send(1, 'load', path=str(path))
    t.check_in(':ok #t', c.response(1))
    for ident, name, line in [(2, 'setting', 3), (3, 'Animal', 4), (4, 'speak', 5)]:
        c.send(ident, 'eval', code="(dev/source 'app.forms/%s)" % name)
        t.check_in(':line %d}' % line, c.response(ident))
    c.send(5, 'eval', code="(dev/source 'app.forms/fake)")
    t.check_in(':value "nil"', c.response(5))


@test('dev concurrent threaded eval')
def concurrent_threaded_eval(t):
    if os.environ.get('OTIUM_VM'):
        raise Skip('threads need native code')
    server = serve(t, watchdog=0)
    a, b = server.client, server.connect()
    a.send(1, 'eval', code='(require otium.thread :as thread) (thread/join (thread/spawn (fn () (thread/sleep 0.2) 42)))')
    b.send(2, 'eval', code='(+ 1 2)')
    t.check_in(':value "3"', b.response(2))
    t.check_in(':value "42"', a.response(1))


@test('dev pause leaves peer running')
def pause_leaves_peer_running(t):
    if os.environ.get('OTIUM_VM'):
        raise Skip('threads need native code')
    server = serve(t, watchdog=0)
    a, b = server.client, server.connect()
    a.send(1, 'eval', code='(require otium.thread :as thread) (def ticks 0) (def peer (thread/spawn (fn () (let loop () (set! ticks (+ ticks 1)) (thread/sleep 0.005) (loop))))) (let loop () (loop))')
    time.sleep(.05)
    a.send(2, 'interrupt', eval=1)
    t.check_in(':event :paused', a.read())
    a.response(2)
    b.send(3, 'eval', code='ticks')
    first = b.response(3)
    time.sleep(.05)
    b.send(4, 'eval', code='ticks')
    second = b.response(4)
    one = int(re.search(r':value "(\d+)"', first).group(1))
    two = int(re.search(r':value "(\d+)"', second).group(1))
    t.check(two > one, 'peer stopped while eval paused', first, second)
    a.send(5, 'abort', eval=1)
    a.response(5)
    t.check_in(':ok #f', a.response(1))
    b.send(6, 'eval', code='(thread/kill! peer)')
    b.response(6)


@test('dev blocked stdout stays responsive')
def blocked_stdout(t):
    if os.environ.get('OTIUM_VM'):
        raise Skip('threads need native code')
    script = t.scratch / 'output.ot'
    script.write_text('(require otium.thread :as thread)\n'
                      '(write-string (string-repeat "x" 100000))\n'
                      '(let wait () (unless (eq? (thread/status otium.dev/%output-worker) :waiting) (thread/yield) (wait)))\n'
                      '(write-string "ready\\n" (current-error-port))\n')
    server = Server(t, 'serve', str(script), watchdog=0)
    ready, _, _ = select.select([server.process.stderr], [], [], 10)
    t.check(ready and server.process.stderr.readline() == b'ready\n', 'output did not park')
    c = server.client
    c.send(1, 'eval', code='(+ 1 2)')
    t.check_in(':value "3"', c.response(1))
    server.process.send_signal(signal.SIGINT)
    server.process.wait(timeout=2)  # Do not drain its full stdout pipe first.
    t.check(server.process.returncode == 130, 'SIGINT did not cancel output forwarding')


@test('dev prepared top-level forms')
def prepared_top_level_forms(t):
    server = serve(t, watchdog=0)
    c = server.client
    c.send(1, 'eval', code='(defonce once-value 10)')
    t.check_in(':value "nil"', c.response(1))
    c.send(2, 'eval', code='(defonce once-value 20) once-value')
    t.check_in(':value "10"', c.response(2))
    if not os.environ.get('OTIUM_VM'):
        # Native preparation tracks namespaces through nested begin forms.
        c.send(3, 'eval', code='(begin (ns prepared.forms) (def a 41)) (+ a 1)')
        t.check_in(':value "42"', c.response(3))
        c.send(4, 'eval', code='(require otium.thread :as thread) (defmacro wait-and-return (x) `(begin (thread/join (thread/spawn (fn () (thread/sleep 0.01)))) ,x)) (wait-and-return 17)')
        t.check_in(':value "17"', c.response(4))


@test('dev eval targets are per client')
def eval_targets_per_client(t):
    if os.environ.get('OTIUM_VM'):
        raise Skip('threads need native code')
    server = serve(t, watchdog=0)
    a, b = server.client, server.connect()
    for c in (a, b):
        c.send(1, 'eval', code='(let loop () (loop))')
        c.send(2, 'status')
        c.response(2)
    for c in (a, b):
        c.send(3, 'interrupt', eval=1)
        t.check_in(':ok #t', c.response(3))
        c.send(4, 'abort', eval=1)
        t.check_in(':ok #t', c.response(4))
        t.check_in(':ok #f', c.response(1))


FRAME_PROGRAM = '''(require (otium.dev :as dev) (otium.thread :as thread))
(def state :idle)
(def frames 0)
(dev/frame-loop! #t)
(let loop ()
  (dev/between-frames!)
  (dev/enter-frame!)
  (set! state :drawing)
  ;; Long enough to be preempted inside the frame.
  (let spin ((i 0)) (when (< i 30000000) (spin (+ i 1))))
  (set! frames (+ frames 1))
  (set! state :idle)
  (dev/leave-frame!)
  (thread/sleep 0.001)
  (loop))
'''


@test('dev evaluations land between frames')
def evaluations_between_frames(t):
    if os.environ.get('OTIUM_VM'):
        raise Skip('threads need native code')
    script = t.scratch / 'frames.ot'
    script.write_text(FRAME_PROGRAM)
    server = Server(t, script, watchdog=0)
    client = server.client
    seen = set()
    for ident in range(1, 41):
        client.send(ident, 'eval', code='state')
        seen.add(re.search(r':value "([^"]*)"', client.response(ident)).group(1))
    t.check(seen == {':idle'}, 'an evaluation ran inside a frame', seen)
    concurrent = set()
    for ident in range(41, 81):
        client.send(ident, 'eval', code='state', concurrent=True)
        concurrent.add(re.search(r':value "([^"]*)"', client.response(ident)).group(1))
    t.check(':drawing' in concurrent, ':concurrent evaluation still waited for frames', concurrent)
    client.send(300, 'eval', code='frames')
    first = int(re.search(r':value "(\d+)"', client.response(300)).group(1))
    time.sleep(.1)
    client.send(301, 'eval', code='frames')
    second = int(re.search(r':value "(\d+)"', client.response(301)).group(1))
    t.check(second > first, 'frames stopped', first, second)
    client.send(302, 'eval', code='(let loop () (loop))')
    time.sleep(.05)
    client.send(303, 'interrupt', eval=302)
    t.check_in(':event :paused', client.read())
    client.response(303)
    client.send(304, 'eval', code='frames', concurrent=True)
    paused_at = int(re.search(r':value "(\d+)"', client.response(304)).group(1))
    client.send(305, 'eval', code='(+ 1 2)')
    t.check_in(':value "3"', client.response(305))
    client.send(306, 'eval', code='frames', concurrent=True)
    later = int(re.search(r':value "(\d+)"', client.response(306)).group(1))
    t.check(later > paused_at, 'a paused evaluation kept the frame lock', paused_at, later)
    client.send(307, 'abort', eval=302)
    client.response(307)
    t.check_in(':ok #f', client.response(302))
