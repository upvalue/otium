#!/usr/bin/env python3
"""Test standard stream lookahead and interruption of a blocking byte read."""
from pathlib import Path
import os
import select
import signal
import subprocess
import tempfile

CLI = Path(os.environ.get('OTIUM_TEST_CLI', Path(__file__).resolve().with_name('otium-proto'))).resolve()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'ports.ot'
    path.write_text('(println (list (peek-char) (read-u8) (read-u8) (read-line) (read-all)))')
    result = subprocess.run([str(CLI), str(path)], input='é🙂\nlast'.encode(), capture_output=True, timeout=10)
    assert result.returncode == 0 and result.stdout.decode() == '(#\\é 195 169 "🙂" "last")\n', result
    path.write_text('(write-u8 255) (write-bytevector #u8(0 1))')
    result = subprocess.run([str(CLI), str(path)], capture_output=True, timeout=10)
    assert result.returncode == 0 and result.stdout == bytes([255, 0, 1]), result
    for source, expected, next_byte in [
        (b'(a)x', b'(a)\n', 120),
        (b'"s"x', b's\n', 120),
        (b"'a x", b'(quote a)\n', 32),
        (b'#\\)x', b')\n', 120),
        (b'#u8(1)x', b'(bytevector 1)\n', 120),
        (b'[1]x', b'(array 1)\n', 120),
    ]:
        path.write_text('(println (read)) (println (read-u8))')
        process = subprocess.Popen([str(CLI), str(path)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            process.stdin.write(source); process.stdin.flush()
            ready, _, _ = select.select([process.stdout], [], [], 10)
            assert ready, 'datum reader waited for EOF'
            first = process.stdout.readline()
            assert first == expected, (source, first)
            # Leave stdin open until the process exits: the sentinel must suffice.
            process.wait(timeout=10)
            rest = process.stdout.read()
            assert rest == f'{next_byte}\n'.encode() and process.returncode == 0, (source, rest)
            assert not process.stderr.read()
        finally:
            if process.poll() is None:
                process.kill(); process.wait()
            process.stdin.close(); process.stdout.close(); process.stderr.close()
    path.write_text('(unwind-protect (begin (println :ready) (read-u8)) (println :closed))')
    process = subprocess.Popen([str(CLI), str(path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        ready, _, _ = select.select([process.stdout], [], [], 10)
        assert ready and process.stdout.readline() == b':ready\n'
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=10)
        assert process.returncode == 130 and stdout == b':closed\n' and not stderr, (process.returncode, stdout, stderr)
    finally:
        if process.poll() is None:
            process.kill(); process.communicate()
print('standard stream lookahead, raw output and blocking interrupt checks passed')
