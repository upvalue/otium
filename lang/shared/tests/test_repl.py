#!/usr/bin/env python3
"""Exercise the shared line editor through both runtime launchers."""

import fcntl
import os
from pathlib import Path
import pty
import select
import shlex
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parents[2]


class Repl:
    def __init__(self, runtime, environment, *args):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.update(environment)
            os.chdir(ROOT)
            os.execv(str(ROOT / runtime), [str(ROOT / runtime), *args])
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
        self.pending = b""
        self.prompt = b"ot> " if runtime == "otium" else b"user> "
        self.expect(self.prompt + b"\x1b[J")

    def send(self, text):
        os.write(self.fd, text)

    def expect(self, text, timeout=15):
        deadline = time.monotonic() + timeout
        while text not in self.pending:
            assert time.monotonic() < deadline, (text, self.pending)
            if select.select([self.fd], [], [], 0.1)[0]:
                try:
                    chunk = os.read(self.fd, 65536)
                except OSError:
                    chunk = b""
                assert chunk, ("unexpected EOF", text, self.pending)
                self.pending += chunk
        end = self.pending.index(text) + len(text)
        result, self.pending = self.pending[:end], self.pending[end:]
        return result

    def result(self, value):
        self.expect(b"\r\n" + str(value).encode() + b"\r\n")
        self.expect(self.prompt + b"\x1b[J")

    def close(self):
        self.send(b"\x04")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                os.close(self.fd)
                self.pid = None
                assert os.waitstatus_to_exitcode(status) == 0, status
                return
            time.sleep(0.02)
        raise AssertionError("Ctrl-D did not exit")

    def cleanup(self):
        if self.pid:
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
            os.close(self.fd)


def exercise(runtime, temp):
    editor = temp / "editor with spaces.py"
    edited = temp / "replacement"
    captured = temp / "captured"
    mode = temp / "mode"
    history = temp / "history"
    editor.write_text('''import os, pathlib, sys, termios
root = pathlib.Path(__file__).parent
assert sys.argv[1] == "--test"
assert termios.tcgetattr(0)[3] & termios.ICANON
path = pathlib.Path(sys.argv[2])
(root / "captured").write_text(path.read_text())
(root / "temp-path").write_text(str(path))
if (root / "mode").read_text() == "fail": sys.exit(1)
path.write_text((root / "replacement").read_text())
''')
    environment = dict(TERM="xterm-256color", VISUAL="", OTIUM_HISTORY=str(history),
                       EDITOR=f"{shlex.quote(sys.executable)} {shlex.quote(str(editor))} --test")
    mode.write_text("ok")
    edited.write_text("(+ 20 22)\n")
    (temp / "replmod.scm").write_text(
        '(define (qualified-fn x) "Qualified documentation." (+ x 10))\n')
    args = ("-L", str(temp)) if runtime == "otium-chez" else ("--path", str(temp))
    repl = Repl(runtime, environment, *args)
    try:
        require = (b"(require replmod :as r)\r" if runtime == "otium-chez"
                   else b"(require '(replmod :as r))\r")
        repl.send(require)
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(r/qualified-\t 5)\r")
        repl.result(15)
        repl.send(b",help r/qualified-fn\r")
        repl.expect(b"Qualified documentation.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b",help +\r")
        repl.expect(b"No documentation recorded." if runtime == "otium-chez" else b"adds numbers.")
        repl.expect(repl.prompt + b"\x1b[J")
        topics = ((b"eval", b"expand Otium macros"),
                  (b"hashtable-ref", b"No documentation recorded.")) if runtime == "otium-chez" else (
                  (b"length", b"Unicode code points"),
                  (b"get", b"Missing entries"),
                  (b"map", b"Kind-preserving"),
                  (b"otium.core/car", b"head of a pair"))
        for name, documentation in topics:
            repl.send(b",help " + name + b"\r")
            repl.expect(documentation)
            repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b'(define documented-value "Value documentation." 42)\r')
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b",help documented-value\r")
        repl.expect(b"Value documentation.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(describe 'documented-value)\r")
        repl.expect(b"Value documentation.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(describe 'r/qualified-fn)\r")
        repl.expect(b"Qualified documentation.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b'(defmacro documented-macro (x) "Macro documentation." x)\r')
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(documented-macro 42)\r")
        repl.result(42)
        repl.send(b",help documented-macro\r")
        repl.expect(b"Macro documentation.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(describe 'ns)\r")
        repl.expect(b"(ns name")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(define car 42)\r")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b",help car\r")
        repl.expect(b"No documentation recorded.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b'(define (repl-square x) "Square the input." (* x x))\r')
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(repl-squ\t 6)\r")
        repl.result(36)
        repl.send(b",help repl-square\r")
        repl.expect(b"Square the input.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b",help definitely-missing-name\r")
        repl.expect(b"No binding or help topic named definitely-missing-name.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b'(define (repl-square x) "Updated square docs." (+ 1 (* x x)))\r')
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b",help repl-square\r")
        repl.expect(b"Updated square docs.")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(+ 1 2)\x18\x05")
        repl.expect(b"\x1b[?2004l")
        repl.expect(b"\x1b[?2004h")
        assert captured.read_text() == "(+ 1 2)"
        assert not Path((temp / "temp-path").read_text()).exists()
        # A second key edit proves the result is back in the editable buffer.
        repl.send(b"\x01\x05\r")
        repl.result(42)
        repl.send(b"\x1b[A\r")
        repl.result(42)
        # Reader-driven continuation and interrupt cancellation.
        repl.send(b"(+ 30\r")
        repl.expect(b"..> ")
        repl.send(b"12)\r")
        repl.result(42)
        repl.send(b"(+ 999\r")
        repl.expect(b"..> ")
        repl.send(b"\x03")
        repl.expect(repl.prompt + b"\x1b[J")
        repl.send(b"(+ 3 4)\r")
        repl.result(7)
        # Failed editors leave the old line intact.
        mode.write_text("fail")
        repl.send(b"(+ 4 5)\x18\x05")
        repl.expect(b"editor failed")
        repl.expect(b"\x1b[?2004h")
        repl.send(b"\r")
        repl.result(9)
        # Multiline file contents pass through each runtime's own reader.
        mode.write_text("ok")
        edited.write_text("(+ 50\n  5)\n")
        repl.send(b"\x18\x05")
        repl.expect(b"\x1b[?2004l")
        repl.expect(b"\x1b[?2004h")
        repl.send(b"\r")
        repl.result(55)
        repl.close()
    finally:
        repl.cleanup()
    assert history.exists()
    repl = Repl(runtime, environment)
    try:
        repl.send(b"\x1b[A\r")
        repl.result(55)
        repl.close()
    finally:
        repl.cleanup()
    # Redirected input must never contain terminal control sequences.
    result = subprocess.run([str(ROOT / runtime)], input="(+ 8\n9)\n", text=True,
                            capture_output=True, env={**os.environ, **environment}, timeout=15)
    assert result.returncode == 0 and "17" in result.stdout, result
    assert "\x1b" not in result.stdout, result
    print(f"{runtime}: editing, completion/help, editor handoff, history, multiline, interrupt, EOF, pipes passed")


with tempfile.TemporaryDirectory(prefix="otium-repl-test-") as directory:
    for runtime in ("otium", "otium-chez"):
        exercise(runtime, Path(directory))
