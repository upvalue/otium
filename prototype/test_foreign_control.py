#!/usr/bin/env python3
"""Check callback abort boundaries and contained exit with the embedding host."""
import os
from pathlib import Path
import signal
import subprocess

DIRECTORY = Path(__file__).resolve().parent
HOST = Path(os.environ.get('OTIUM_TEST_FOREIGN', DIRECTORY / 'test_foreign')).resolve()
OPTIONS = ['--sanitize'] if HOST.name.endswith('_sanitize') else []
for mode, message in [('outside', 'outside an Otium foreign call'),
                      ('thread', 'thread other than the Otium thread')]:
    result = subprocess.run([str(HOST), *OPTIONS, '--callback-' + mode],
                            cwd=DIRECTORY, text=True, capture_output=True, timeout=30)
    assert result.returncode == -signal.SIGABRT, result
    assert message in result.stderr, result
    assert 'AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr, result
result = subprocess.run([str(HOST), *OPTIONS, '--callback-exit'], cwd=DIRECTORY,
                        text=True, capture_output=True, timeout=30)
assert result.returncode == 0, result
assert result.stdout == 'callback exit and outer cleanup passed\n', result
print('callback thread, idle host and exit boundary checks passed')
