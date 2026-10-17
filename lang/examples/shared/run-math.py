#!/usr/bin/env python3
"""Run the shared math-library example after `make ffi`."""
import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime', choices=('c', 'chez'), default='c')
args = parser.parse_args()
mac = platform.system() == 'Darwin'
names = '("/usr/lib/libSystem.B.dylib")' if mac else '("libm.so.6" "libm.so")'
prefix = "(require '(otium.ffi :as ffi))\n" if args.runtime == 'c' else '(require otium.ffi :as ffi)\n'
source = prefix + "(define math-library-names '" + names + ')\n' + (Path(__file__).parent / 'math.scm').read_text()
helper = ROOT / 'shared/build' / (platform.machine() + '-' + platform.system().lower()) / ('libotium-ffi.' + ('dylib' if mac else 'so'))
command = [str(ROOT / 'c/otium'), '--no-project'] if args.runtime == 'c' else [os.environ.get('CHEZ', 'chez'), '--libdirs', str(ROOT / 'chez'), '--script', str(ROOT / 'chez/main.ss')]
with tempfile.TemporaryDirectory(prefix='otium-math-') as directory:
    script = Path(directory) / 'math.scm'
    script.write_text(source)
    result = subprocess.run(command + [str(script)], cwd=ROOT,
                            env=dict(os.environ, OTIUM_FFI_LIBRARY=str(helper)))
    raise SystemExit(result.returncode)
