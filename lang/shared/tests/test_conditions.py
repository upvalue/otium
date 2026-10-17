#!/usr/bin/env python3
"""Run the same condition semantics against C and Chez."""
import argparse
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile

from otium_tools import command as runtime_command

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "conditions"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--runtime", choices=("c", "chez", "both"), default="both")
args = parser.parse_args()
runtimes = ("c", "chez") if args.runtime == "both" else (args.runtime,)


def command(runtime, path):
    return runtime_command(runtime, path)


def execute(runtime, path, status, output):
    result = subprocess.run(command(runtime, path), cwd=ROOT, text=True,
                            capture_output=True, timeout=30)
    assert result.returncode == status, (runtime, result.returncode, result.stdout, result.stderr)
    assert result.stdout == output, (runtime, result.stdout, result.stderr)


with tempfile.TemporaryDirectory(prefix="otium-condition-tests-") as directory:
    directory = Path(directory)
    for runtime in runtimes:
        bootstrap = (FIXTURES / "support.ot").read_text()
        path = directory / f"{runtime}.ot"
        path.write_text(bootstrap + "\n" + (FIXTURES / "common.ot").read_text())
        execute(runtime, path, 0, "condition checks: 64\n")
        for operation in ("exit", "quit"):
            path.write_text(bootstrap + f'''
(handler-bind (((fn (c) #t) (fn (c) (test-print 999))))
  (try (unwind-protect ({operation} 7) (test-print 7))
    (catch ((fn (c) #t) c) (test-print 998))))
''')
            execute(runtime, path, 7, "condition checks: 7\n")
        path.write_text(bootstrap + '''
(try (unwind-protect (exit 7) (error 'cleanup))
  (catch ((fn (c) #t) c) (test-print 8)))
''')
        execute(runtime, path, 0, "condition checks: 8\n")

        marker = "x" * 32768
        mark = f'(display "{marker}")'
        path.write_text(bootstrap + f'''
(def (spin) (spin))
(handler-bind (((fn (c) #t) (fn (c) (test-print 999))))
  (try (unwind-protect (begin {mark} (spin)) (test-print 9))
    (catch ((fn (c) #t) c) (test-print 998))))
''')
        process = subprocess.Popen(command(runtime, path), cwd=ROOT, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            ready, _, _ = select.select([process.stdout], [], [], 10)
            assert ready, (runtime, "did not reach interrupt test")
            first = os.read(process.stdout.fileno(), 1).decode()
            assert first == "x", (runtime, "exited before protected body")
            process.send_signal(signal.SIGINT)
            stdout, stderr = process.communicate(timeout=10)
            output = first + stdout
            suffix = "condition checks: 9\n"
            assert output.endswith(suffix), (runtime, output[-200:], stderr)
            assert set(output[:-len(suffix)]) == {"x"}, (runtime, output[-200:], stderr)
            assert process.returncode != 0, (runtime, process.returncode)
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        print(f"{runtime}: 64 condition checks, quit/exit, cleanup replacement, interrupt passed")
