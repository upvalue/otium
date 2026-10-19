#!/usr/bin/env python3
"""Socket protocol and cooperative interruption, on both runtimes."""
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time
import unittest

from otium_tools import dev

ROOT = Path(__file__).resolve().parents[2]
RUNTIMES = os.environ.get('OTIUM_TEST_RUNTIMES', 'chez c').split()


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
            ' :%s %s' % (key.replace('_', '-'), json.dumps(value, ensure_ascii=False)) for key, value in fields.items()))
        data = text.encode('utf-8')
        self.socket.sendall(struct.pack('!I', len(data)) + data)

    def read(self):
        def exact(size):
            data = b''
            while len(data) < size:
                part = self.socket.recv(size - len(data))
                if not part:
                    raise AssertionError('server disconnected')
                data += part
            return data
        size, = struct.unpack('!I', exact(4))
        return exact(size).decode('utf-8')

    def response(self, ident):
        while True:
            data = self.read()
            if data.startswith('{:event '):
                self.events.append(data)
            else:
                assert re.search(r':id %d(?:\s|})' % ident, data), data
                return data


class Server:
    def __init__(self, runtime, watchdog=2000, repl=False, stress=False):
        self.runtime = runtime
        self.directory = tempfile.TemporaryDirectory(prefix='otdev-', dir='/tmp')
        self.path = Path(self.directory.name) / 'dev.sock'
        self.clients = []
        env = dict(os.environ)
        if stress:
            env['OTIUM_GC_STRESS'] = '1'
        self.process = subprocess.Popen(
            [str(ROOT / 'otium'), '--runtime=' + runtime, '--no-project',
             '--dev=' + str(self.path), '--dev-watchdog=' + str(watchdog)] + ([] if repl else ['serve']),
            cwd=ROOT, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for _ in range(400):
            if self.path.exists():
                break
            if self.process.poll() is not None:
                raise AssertionError(self.process.communicate())
            time.sleep(.025)
        self.client = self.connect()

    def connect(self):
        client = Client(self.path)
        self.clients.append(client)
        return client

    def close(self):
        for client in self.clients:
            client.close()
        self.process.terminate()
        try:
            self.process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate()
        self.directory.cleanup()


class DevelopmentServerTests(unittest.TestCase):
    def servers(self, **kwargs):
        for runtime in RUNTIMES:
            with self.subTest(runtime=runtime, **kwargs):
                server = Server(runtime, **kwargs)
                try:
                    yield server
                finally:
                    server.close()

    def test_eval_load_errors_clients(self):
        for server in self.servers():
            c = server.client
            self.assertEqual(server.path.stat().st_mode & 0o777, 0o700)
            c.send(1, 'eval', code='(println "héllo") (+ 1 2)')
            result = c.response(1)
            self.assertIn(':value "3"', result)
            self.assertIn(':output "héllo\\n"', result)
            c.send(2, 'eval', ns='app.state', code='(defonce value 41)')
            self.assertIn(':ok #t', c.response(2))
            other = server.connect()
            other.send(3, 'eval', ns='app.state', code='(+ value 1)')
            self.assertIn(':value "42"', other.response(3))
            path = Path(server.directory.name) / 'reload.ot'
            path.write_text('(ns app.state)\n(def value 12)\n(error "broken")\n(def value 99)\n')
            c.send(4, 'load', path=str(path))
            failure = c.response(4)
            self.assertIn(':message "broken"', failure)
            self.assertIn(':line 3', failure)
            self.assertIn(str(path), failure)
            other.send(5, 'eval', ns='app.state', code='value')
            self.assertIn(':value "12"', other.response(5))
            c.send(6, 'eval', code='(println "before") (error "oops" 1)')
            result = c.response(6)
            self.assertIn(':ok #f', result)
            self.assertIn(':output "before\\n"', result)
            c.send(7, 'eval', code='(+ 2 3)')
            self.assertIn(':value "5"', c.response(7))
            path.write_text('(ns app.state)\n(def value 20)\n(+ value 2)\n')
            c.send(8, 'load', path=str(path))
            self.assertIn(':value "22"', c.response(8))
            c.send(9, 'eval', code='(error #f)')
            self.assertIn(':ok #f', c.response(9))

    def test_interrupt_continue_abort(self):
        for server in self.servers(watchdog=0):
            c = server.client
            c.send(1, 'eval', code='(def stop #f) (def cleaned #f) (unwind-protect (while (not stop) nil) (set! cleaned #t)) 99')
            time.sleep(.1)
            start = time.monotonic()
            c.send(2, 'interrupt')
            self.assertIn(':event :paused', c.read())
            self.assertLess(time.monotonic() - start, .1)
            self.assertIn(':ok #t', c.response(2))
            c.send(3, 'eval', code='(set! stop #t)')
            self.assertIn(':ok #t', c.response(3))
            c.send(4, 'continue')
            self.assertIn(':ok #t', c.response(4))
            self.assertIn(':value "99"', c.response(1))
            c.send(5, 'eval', code='cleaned')
            self.assertIn(':value "#t"', c.response(5))
            c.send(6, 'eval', code='(set! cleaned #f) (unwind-protect (let loop () (cons 1 2) (loop)) (set! cleaned #t))')
            time.sleep(.1)
            start = time.monotonic()
            c.send(7, 'interrupt')
            self.assertIn(':event :paused', c.read())
            self.assertLess(time.monotonic() - start, .1)
            c.response(7)
            # Disconnecting the interrupting client leaves the process paused.
            replacement = server.connect()
            c.close()
            replacement.send(8, 'status')
            self.assertIn(':state :paused', replacement.response(8))
            replacement.send(9, 'abort')
            replacement.response(9)
            replacement.send(10, 'eval', code='cleaned')
            self.assertIn(':value "#t"', replacement.response(10))

    def test_watchdog_and_malformed(self):
        for server in self.servers(watchdog=150):
            c = server.client
            c.send(1, 'eval', code='(let loop () (loop))')
            start = time.monotonic()
            self.assertIn(':reason :watchdog', c.read())
            self.assertLess(time.monotonic() - start, .3)
            c.send(2, 'abort')
            c.response(2)
            self.assertIn(':ok #f', c.response(1))
            raw = b'{invalid'
            c.socket.sendall(struct.pack('!I', len(raw)) + raw)
            self.assertIn(':ok #f', c.read())
            c.send(3, 'status')
            self.assertIn(':ok #t', c.response(3))
            c.send(4, 'continue')
            self.assertIn(':ok #f', c.response(4))

    def test_gc_stress(self):
        for server in self.servers(stress=True):
            c = server.client
            for i in range(8):
                c.send(i, 'eval', ns='stress.space', code='(defonce x (list 1 2 3)) (println x) (cons x x)')
                result = c.response(i)
                self.assertIn(':ok #t', result)
                self.assertIn(':output "(1 2 3)\\n"', result)

    def test_repl_idle(self):
        for server in self.servers(repl=True):
            c = server.client
            c.send(1, 'eval', code='(+ 20 22)')
            self.assertIn(':value "42"', c.response(1))
            server.process.stdin.write(b'(println "background-output")\n')
            server.process.stdin.flush()
            while 'background-output' not in c.read():
                pass

    def test_nested_pause(self):
        for server in self.servers(watchdog=150):
            c = server.client
            c.send(1, 'eval', code='(let loop () (loop))')
            self.assertIn(':event :paused', c.read())
            c.send(2, 'eval', code='(let loop () (loop))')
            self.assertIn(':event :paused', c.read())
            c.send(3, 'abort')
            c.response(3)
            self.assertIn(':ok #f', c.response(2))
            c.send(4, 'status')
            self.assertIn(':state :paused', c.response(4))
            c.send(5, 'abort')
            c.response(5)
            self.assertIn(':ok #f', c.response(1))

    def test_fragmented_frames_and_bad_clients(self):
        for server in self.servers():
            c = server.client
            payload = b'{:id 1 :op :status}'
            frame = struct.pack('!I', len(payload)) + payload
            for byte in frame:
                c.socket.sendall(bytes([byte]))
            self.assertIn(':ok #t', c.response(1))
            for invalid in (b'\xff', b'\xc0\x80', b'\xed\xa0\x80', b'a\0b', b'\xf0\x9f\x98'):
                other = server.connect()
                other.socket.sendall(struct.pack('!I', len(invalid)) + invalid)
                self.assertEqual(other.socket.recv(1), b'')
            other = server.connect()
            other.socket.sendall(struct.pack('!I', 4 * 1024 * 1024 + 1))
            self.assertEqual(other.socket.recv(1), b'')
            c.send(2, 'status')
            self.assertIn(':ok #t', c.response(2))

    def test_attach_and_mcp(self):
        for server in self.servers():
            result = subprocess.run([str(ROOT/'otium'), 'attach', str(server.path), '--eval', '(+ 20 22)'],
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(':value "42"', result.stdout)
            process = subprocess.Popen([str(ROOT/'otium'), 'mcp', str(server.path)],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                messages = [
                    {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {'protocolVersion': '2024-11-05'}},
                    {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
                    {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {'name': 'eval', 'arguments': {'code': '(+ 3 4)'}}},
                ]
                output, error = process.communicate(''.join(json.dumps(message) + '\n' for message in messages), timeout=10)
                self.assertEqual(process.returncode, 0, error)
                responses = [json.loads(line) for line in output.splitlines()]
                self.assertEqual(responses[0]['id'], 1)
                self.assertEqual(len(responses[1]['result']['tools']), 6)
                response = responses[2]
                self.assertEqual(response['id'], 3)
                self.assertIn(':value "7"', response['result']['content'][0]['text'])
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.communicate(timeout=3)

    def test_events_truncation_and_eval_path(self):
        for server in self.servers(watchdog=150):
            c = server.client
            c.send(1, 'eval', code='(let loop () (loop))')
            self.assertIn(':event :paused', c.read())
            c.send(2, 'abort')
            self.assertIn(':ok #t', c.response(2))
            c.response(1)
            c.send(3, 'events', since=0)
            result = c.response(3)
            self.assertRegex(result, r':event :paused .*:seq 1')
            self.assertRegex(result, r':event :resumed .*:seq 2')
            self.assertIn(':event 2}', result)
            c.send(4, 'events', since=1)
            self.assertNotIn(':paused', c.response(4))
            c.send(5, 'eval', code='"abcdefghij"', max_chars=4)
            result = c.response(5)
            self.assertIn(':value "\\"abc"', result)
            self.assertIn(':truncated #t', result)
            c.send(6, 'eval', code='(+ 1 2)')
            self.assertNotIn(':truncated', c.response(6))
            c.send(7, 'eval', code='\n(error "here")', path='/tmp/scratch.ot')
            self.assertIn(':source {:path "/tmp/scratch.ot" :line 2}', c.response(7))
            c.send(8, 'status')
            result = c.response(8)
            self.assertIn(':pid ', result)
            self.assertIn(':runtime :' + server.runtime, result)

    def test_introspection(self):
        for stress in (False, True):
            for server in self.servers(stress=stress):
                c = server.client
                path = Path(server.directory.name) / 'demo.ot'
                path.write_text('(ns app.demo)\n\n(defonce model {:count 0})\n\n'
                                '(def (greet name)\n  "Say hi."\n  (str "hi " name))\n\n'
                                '(def- (helper & xs) xs)\n\n(defmacro twice (x) (list (quote begin) x x))\n')
                c.send(1, 'load', path=str(path))
                self.assertIn(':ok #t', c.response(1))
                c.send(2, 'eval', code='(require otium.dev) (otium.dev/bindings (quote app.demo))')
                result = c.response(2)
                self.assertIn('{:name greet :kind :function', result)
                self.assertIn(':arity 1 :variadic #f', result.replace('\\', ''))
                self.assertIn('{:name helper :kind :function', result)
                self.assertIn(':public #f', result)
                self.assertIn('{:name twice :kind :macro', result)
                self.assertIn('{:name model :kind :value', result)
                self.assertIn('Say hi.', result)
                c.send(3, 'eval', code='(otium.dev/namespaces)')
                self.assertIn('{:name app.demo :file \\"%s\\"}' % path.resolve(), c.response(3))
                c.send(4, 'eval', code='(otium.dev/apropos "gree")')
                self.assertIn(':value "(app.demo/greet)"', c.response(4))
                c.send(5, 'eval', code='(otium.dev/source (quote app.demo/greet))')
                self.assertIn(':line 5}', c.response(5))
                c.send(6, 'eval', code='(otium.dev/source (quote app.demo/missing))')
                self.assertIn(':value "#f"', c.response(6))
                c.send(7, 'eval', code='(otium.dev/stale-files)')
                self.assertIn(':value "()"', c.response(7))
                future = time.time() + 60
                os.utime(path, (future, future))
                c.send(8, 'eval', code='(otium.dev/stale-files)')
                self.assertIn(str(path.resolve()), c.response(8))

    def test_live_socket_collision(self):
        for server in self.servers():
            result = subprocess.run([str(ROOT / 'otium'), '--runtime=chez', '--dev=' + str(server.path), 'serve'],
                                    capture_output=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            server.client.send(1, 'status')
            self.assertIn(':ok #t', server.client.response(1))


class ClientTests(unittest.TestCase):
    """otium-dev against a server on the default socket path."""

    def start(self, runtime, directory):
        before = set(dev.DIRECTORY.glob('*.sock')) if dev.DIRECTORY.is_dir() else set()
        process = subprocess.Popen(
            [str(ROOT / 'otium'), '--runtime=' + runtime, '--no-project', '--dev-watchdog=200', 'serve'],
            cwd=directory, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(400):
            new = set(dev.DIRECTORY.glob('*.sock')) - before if dev.DIRECTORY.is_dir() else set()
            if new:
                return process, new.pop()
            if process.poll() is not None:
                raise AssertionError(process.stderr.read())
            time.sleep(.025)
        raise AssertionError('server did not start')

    def client(self, directory, *args, stdin=None):
        return subprocess.run([str(ROOT / 'tools/otium-dev'), *args], cwd=directory, input=stdin,
                              capture_output=True, text=True, timeout=20)

    def test_client(self):
        for runtime in RUNTIMES:
            with self.subTest(runtime=runtime), tempfile.TemporaryDirectory(prefix='otdev-') as name:
                directory = Path(name)
                process, path = self.start(runtime, directory)
                try:
                    self.check_client(runtime, directory, process, path)
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                    process.stderr.close()

    def check_client(self, runtime, directory, process, path):
        self.assertEqual(path.parent.stat().st_mode & 0o777, 0o700)

        result = self.client(directory, 'ls')
        self.assertIn(str(directory.resolve()), result.stdout)
        result = self.client(directory, 'status')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('runtime: ' + runtime, result.stdout)

        result = self.client(directory, 'eval', '(println "out") (+ 1 2)')
        self.assertEqual((result.returncode, result.stdout), (0, 'out\n3\n'), result.stderr)
        result = self.client(directory, 'eval', '-', stdin='(* 6 7)')
        self.assertEqual(result.stdout, '42\n')

        (directory / 'scratch.ot').write_text('(+ 1\n  (error "broken"))\n')
        result = self.client(directory, 'eval', '--file', 'scratch.ot')
        self.assertEqual(result.returncode, 1)
        self.assertIn('error: broken', result.stderr)
        self.assertIn('scratch.ot:1', result.stderr)

        (directory / 'demo.ot').write_text('(ns app.demo)\n(def (greet name) (str "hi " name))\n')
        result = self.client(directory, 'load', 'demo.ot')
        self.assertEqual((result.returncode, result.stdout), (0, 'loaded 2 forms\n'), result.stderr)
        result = self.client(directory, 'eval', '--ns', 'app.demo', '(greet "you")')
        self.assertEqual(result.stdout, '"hi you"\n')

        result = self.client(directory, 'eval', '--max-chars', '3', '"abcdef"')
        self.assertEqual(result.stdout, '"ab\n')
        self.assertIn('truncated', result.stderr)

        result = self.client(directory, 'eval', '(let loop () (loop))')
        self.assertEqual(result.returncode, 4, result.stderr)
        self.assertIn('paused (watchdog', result.stderr)
        result = self.client(directory, 'eval', '(+ 2 2)')
        self.assertEqual(result.stdout, '4\n')
        result = self.client(directory, 'abort')
        self.assertEqual((result.returncode, result.stdout), (0, 'aborted\n'), result.stderr)
        result = self.client(directory, 'events')
        self.assertIn('#1 paused reason=:watchdog', result.stdout)
        self.assertIn('#2 resumed', result.stdout)

        result = self.client(directory, 'eval', '--timeout', '0.5', '(require otium.dev) (otium.dev/%poll 2000)')
        self.assertEqual(result.returncode, 3, result.stderr)

        process.kill()
        process.wait()
        self.assertTrue(path.exists())
        self.assertNotIn(path, [found for found, _ in dev.servers()])
        self.assertFalse(path.exists())


if __name__ == '__main__':
    unittest.main()
