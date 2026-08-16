#!/usr/bin/env python3

import subprocess
import sys


otium = sys.argv[1]
fixture = sys.argv[2]


def run(*args, input_text=""):
    return subprocess.run(
        [otium, *args], input=input_text, text=True, capture_output=True, check=False
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

multiline = run(input_text="(+ 1\n 2)\nnil\n(quit)\n(println \"after-quit\")\n")
expect(multiline, 0, stdout="3\n")
assert "nil\n" in multiline.stdout, multiline
assert "after-quit" not in multiline.stdout, multiline
assert multiline.stderr == "", multiline

exit_alias = run(input_text="(exit)\n(println \"after-exit\")\n")
expect(exit_alias, 0)
assert "after-exit" not in exit_alias.stdout, exit_alias
assert exit_alias.stderr == "", exit_alias

single_eval = run(
    input_text=(
        "(define hits (array)) (push! hits 1) (+ 1\n"
        " 2) (println \"hits:\" (length hits))\n"
        "(quit)\n"
    )
)
expect(single_eval, 0, stdout="hits: 1\n")

multiline_string = run(input_text='(println "a\n   \nb")\n(quit)\n')
expect(multiline_string, 0, stdout="a\n   \nb\n")

incomplete_eof = run(input_text="(+ 1\n")
expect(incomplete_eof, 0)
assert "read error" not in incomplete_eof.stderr, incomplete_eof
