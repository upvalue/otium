#!/usr/bin/env python3
"""Deliver SIGINT only after protected code has written its readiness marker."""
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get('OTIUM_TEST_CLI', ROOT / 'prototype/otium-proto')).resolve()
SETUP = '''(def (spin) (spin))
(def (double s n) (if (= n 0) s (double (string-append s s) (- n 1))))
(def (ready) (display (double "x" 15)))
'''


def interrupt(args, expected, status):
    process = subprocess.Popen([str(CLI), *map(str, args)], cwd=ROOT,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        readable, _, _ = select.select([process.stdout], [], [], 15)
        assert readable, 'protected body did not become ready'
        first = os.read(process.stdout.fileno(), 1)
        assert first == b'x', (first, process.poll())
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=15)
        output = first + stdout
        assert output.endswith(expected), (output[-250:], stderr)
        assert set(output[:-len(expected)]) == {ord('x')}, output[-250:]
        assert process.returncode == status, (process.returncode, stderr)
        assert not stderr, stderr
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()


interrupt(['--load', 'tests/conditions/support.ot', 'tests/conditions/interrupt.ot'],
          b'condition checks: 9\n', 130)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'interrupt.ot'
    cases = [
        ('(unwind-protect (unwind-protect (begin (ready) (spin)) (println :inner)) (println :outer))', b':inner\n:outer\n', 130),
        ('(call-with-cleanup (fn () (ready) (spin)) (fn (outcome) (println outcome)))', b':quit\n', 130),
        ('(try (unwind-protect (begin (ready) (spin)) (error :replacement)) (catch e (println e)))', b':replacement\n', 0),
        ('(try (call-with-cleanup (fn () (ready) (spin)) (fn (o) (println o) (error :replacement))) (catch e (println e)))', b':quit\n:replacement\n', 0),
        ("(restart-case (unwind-protect (begin (ready) (spin)) (invoke-restart 'done)) (done () (println :recovered)))", b':recovered\n', 0),
        ("(unwind-protect (eval '(begin (ready) (spin))) (println :eval-cleanup))", b':eval-cleanup\n', 130),
    ]
    for source, expected, status in cases:
        path.write_text(SETUP + source)
        interrupt([path], expected, status)
print('SIGINT cleanup, replacement and nested evaluation checks passed')
