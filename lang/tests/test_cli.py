#!/usr/bin/env python3

import json
import os
import re
import select
import signal
import subprocess
import sys
import time


otium = sys.argv[1]
fixture = sys.argv[2]
second_fixture = sys.argv[3]


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
expect(run("--help"), 0, stdout="--server")
expect(run("--help"), 0, stdout="--heap-max")
expect(run("--help"), 0, stdout="--gc-stats")
expect(run(), 0, stdout="otium repl")

file_only = run(fixture)
expect(file_only, 0, stdout="loaded-from-file\n")
assert "otium repl" not in file_only.stdout, file_only

expect(run(fixture, "--repl"), 0, stdout="loaded-from-file\notium repl")
expect(run("--repl", fixture), 0, stdout="loaded-from-file\notium repl")
multiple_files = run(fixture, second_fixture)
expect(multiple_files, 0, stdout="loaded-from-file\nloaded-from-second-file\n")
assert "otium repl" not in multiple_files.stdout, multiple_files
expect(run("--repl", fixture, second_fixture), 0,
       stdout="loaded-from-file\nloaded-from-second-file\notium repl")
expect(run("--unknown"), 2, stderr="unknown option --unknown")
expect(run("--path"), 2, stderr="--path requires a directory")
expect(run("--repl", "--server"), 2, stderr="cannot be used together")


def frame(source):
    return source + "\n\x1f\n"


server = run(
    "--server",
    input_text=(
        frame("(+ 1 2)")
        + frame('(restart-case (error "server") (use-value (v) v))')
        + frame("(+ 3 4)")
    ),
)
expect(server, 0, stdout="3\n\x1eot> ")
assert server.stdout.count("\x1eot> ") == 3, server
assert "error:" in server.stdout, server
assert "7\n\x1eot> " in server.stdout, server
assert "restart #?" not in server.stdout, server
assert server.stderr == "", server


def read_server_response(process):
    marker = b"\x1eot> "
    output = b""
    deadline = time.monotonic() + 2
    while marker not in output:
        timeout = deadline - time.monotonic()
        assert timeout > 0, ("server response timed out", output, process.poll())
        ready, _, _ = select.select([process.stdout], [], [], timeout)
        assert ready, ("server response timed out", output, process.poll())
        chunk = os.read(process.stdout.fileno(), 4096)
        assert chunk, ("server closed its output", output, process.poll())
        output += chunk
    return output.decode()


interactive = subprocess.Popen(
    [otium, "--no-project", "--server"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
)
try:
    interactive.stdin.write(frame("(+ 1 1)").encode())
    interactive.stdin.flush()
    assert "2\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(
        frame(
            "(begin (define counter 0) (define cleaned #f) "
            "(unwind-protect "
            "  (while #t (set! counter (+ counter 1))) "
            "  (set! cleaned #t)))"
        ).encode()
    )
    interactive.stdin.flush()
    time.sleep(0.05)
    interactive.send_signal(signal.SIGINT)
    assert "paused\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(frame("(not (nil? (find-restart 'continue)))").encode())
    interactive.stdin.flush()
    assert "#t\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(frame("(invoke-restart 'continue)").encode())
    interactive.stdin.flush()
    time.sleep(0.05)
    assert interactive.poll() is None, interactive.poll()
    interactive.send_signal(signal.SIGINT)
    assert "paused\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(frame("(invoke-restart 'abort)").encode())
    interactive.stdin.flush()
    assert "interrupted\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(frame("cleaned").encode())
    interactive.stdin.flush()
    assert "#t\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(frame("counter").encode())
    interactive.stdin.flush()
    counter_response = read_server_response(interactive)
    assert int(counter_response.splitlines()[0]) > 0, counter_response

    interactive.stdin.write(frame("(while #t nil)").encode())
    interactive.stdin.flush()
    time.sleep(0.05)
    interactive.send_signal(signal.SIGINT)
    assert "paused\n\x1eot> " in read_server_response(interactive)
    interactive.send_signal(signal.SIGINT)
    assert "interrupted\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(frame("(+ 20 22)").encode())
    interactive.stdin.flush()
    assert "42\n\x1eot> " in read_server_response(interactive)

    interactive.stdin.write(frame("(quit)").encode())
    interactive.stdin.flush()
    remaining, error_output = interactive.communicate(timeout=2)
    assert interactive.returncode == 0, interactive.returncode
    assert remaining == b"", remaining
    assert error_output == b"", error_output
finally:
    if interactive.poll() is None:
        interactive.kill()
        interactive.communicate()

expect(run("--max-depth"), 2, stderr="--max-depth requires a value")
expect(run("--max-depth", "nope"), 2, stderr="requires an integer")
expect(run("--stack-slots", "0"), 2, stderr="requires an integer")
expect(run("--heap-init", "512"), 2, stderr="must be at least 1024")
expect(run("--heap-max", "1024"), 2, stderr="must be at least --heap-init")

limited = run(
    "--max-depth",
    "1024",
    "--stack-slots",
    "8192",
    "--heap-init",
    str(1 << 20),
    "--heap-max",
    str(8 << 20),
    fixture,
)
expect(limited, 0, stdout="loaded-from-file\n")

gc_stats = run("--gc-stats", fixture)
expect(gc_stats, 0, stdout="loaded-from-file\n", stderr="GC stats:\n")
for field in ("allocations", "collections"):
    assert re.search(rf"^  {field}: \d+$", gc_stats.stderr, re.MULTILINE), gc_stats
for field in (
    "allocated bytes",
    "copied bytes",
    "reclaimed bytes",
    "heap used bytes",
    "peak heap used bytes",
    "heap capacity bytes",
):
    assert re.search(
        rf"^  {field}: \d+ \(\d+(?:\.\d{{2}})? (?:B|KiB|MiB|GiB|TiB|PiB|EiB)\)$",
        gc_stats.stderr,
        re.MULTILINE,
    ), gc_stats
capacity_match = re.search(
    r"^  heap capacity bytes: (\d+) ", gc_stats.stderr, re.MULTILINE
)
assert capacity_match and int(capacity_match.group(1)) > 0, gc_stats

gc_stats_json = run("--gc-stats-json", "--gc-force-compact", fixture)
expect(gc_stats_json, 0, stdout="loaded-from-file\n", stderr="OTIUM_GC_STATS ")
json_line = next(
    line.removeprefix("OTIUM_GC_STATS ")
    for line in gc_stats_json.stderr.splitlines()
    if line.startswith("OTIUM_GC_STATS ")
)
json_stats = json.loads(json_line)
for field in (
    "allocations",
    "collections",
    "used_bytes",
    "reserved_bytes",
    "metadata_bytes",
    "minor_max_pause_ns",
    "major_compact_max_pause_ns",
    "mutator_pause_max_ns",
):
    assert isinstance(json_stats[field], int) and json_stats[field] >= 0, json_stats

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
