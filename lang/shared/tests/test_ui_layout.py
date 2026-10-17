#!/usr/bin/env python3
"""Run the checked-in Otium layout tests on both runtimes without a display."""
import argparse
import os
from pathlib import Path
import platform
import subprocess

from otium_tools import runtime_argument, runtimes, command as runtime_command, environment

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
runtime_argument(parser)
args = parser.parse_args()

for runtime, stress in [(r, stress) for r in runtimes(args.runtime)
                        for stress in ((False, True) if r == 'c' else (False,))]:
    env = environment(runtime=runtime, stress=stress)
    result = subprocess.run(runtime_command(runtime) + [ '--path', str(ROOT/'shared/lib'),
                             str(ROOT/'shared/tests/ui/layout.ot')],
                            env=env, cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, (runtime, stress, result.stdout, result.stderr)
    assert 'UI layout checks:' in result.stdout, result.stdout
    print(runtime + (' (GC stress)' if stress else '') + ': ' + result.stdout.strip())
