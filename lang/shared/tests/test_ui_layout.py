#!/usr/bin/env python3
"""Run the checked-in Otium layout tests on both runtimes without a display."""
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TARGET = platform.machine() + '-' + platform.system().lower()
EXT = 'dylib' if platform.system() == 'Darwin' else 'so'
for runtime, stress in [('c', False), ('c', True), ('chez', False)]:
    env = dict(os.environ, OTIUM_FFI_LIBRARY=str(ROOT/'shared/build'/TARGET/('libotium-ffi.'+EXT)))
    if stress:
        env['OTIUM_GC_STRESS'] = '1'
    result = subprocess.run([str(ROOT/('c/otium' if runtime == 'c' else 'chez/run')),
                             '--no-project', '--path', str(ROOT/'shared/lib'),
                             str(ROOT/'shared/tests/ui/layout.scm')],
                            env=env, cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, (runtime, stress, result.stdout, result.stderr)
    assert 'UI layout checks:' in result.stdout, result.stdout
    print(runtime + (' (GC stress)' if stress else '') + ': ' + result.stdout.strip())
