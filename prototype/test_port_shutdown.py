#!/usr/bin/env python3
"""Check buffered custom output at script completion, exit, and quit."""
from pathlib import Path
import os
import subprocess
import tempfile

CLI = Path(os.environ.get('OTIUM_TEST_CLI', Path(__file__).resolve().with_name('otium-proto'))).resolve()
SETUP = '''(def original-output (current-output-port))
(def sink (make-output-port "sink"
  {:write (fn (bytes start end)
            (write-bytevector bytes original-output start end)
            (- end start))}))
(write-string "buffered" sink)
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'shutdown.ot'
    for suffix, status in [('', 0), ('(exit 7)', 7), ('(quit 9)', 9)]:
        path.write_text(SETUP + suffix)
        result = subprocess.run([str(CLI), str(path)], capture_output=True, timeout=10)
        assert result.returncode == status and result.stdout == b'buffered' and not result.stderr, result
    path.write_text('(def p (make-output-port "fail" {:write (fn (b s e) (error "write failed"))})) (write-u8 1 p) (exit 7)')
    result = subprocess.run([str(CLI), str(path)], capture_output=True, timeout=10)
    assert result.returncode == 1 and b'write failed' in result.stderr, result
    path.write_text('(def p (make-output-port "exit in flush" {:flush (fn () (exit 12)) :write (fn (b s e) (- e s))}))')
    result = subprocess.run([str(CLI), str(path)], capture_output=True, timeout=10)
    assert result.returncode == 12 and not result.stderr, result
print('script completion, exit, quit and failing shutdown flush checks passed')
