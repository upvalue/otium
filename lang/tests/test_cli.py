#!/usr/bin/env python3

import subprocess
import sys


otium = sys.argv[1]
fixture = sys.argv[2]


def run(*args):
    return subprocess.run(
        [otium, *args], input="", text=True, capture_output=True, check=False
    )


def expect(result, status, stdout=None, stderr=None):
    assert result.returncode == status, result
    if stdout is not None:
        assert stdout in result.stdout, result
    if stderr is not None:
        assert stderr in result.stderr, result


expect(run("--help"), 0, stdout="--repl")
expect(run(), 0, stdout="otium repl")

file_only = run(fixture)
expect(file_only, 0, stdout="loaded-from-file\n")
assert "otium repl" not in file_only.stdout, file_only

expect(run(fixture, "--repl"), 0, stdout="loaded-from-file\notium repl")
expect(run("--repl", fixture), 0, stdout="loaded-from-file\notium repl")
expect(run("--unknown"), 2, stderr="unknown option --unknown")
expect(run("--path"), 2, stderr="--path requires a directory")
