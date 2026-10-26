#!/usr/bin/env python3
"""Compare every token field in the public reader fixtures with Chez."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parent.parent
commands = [[ROOT / 'prototype/otium-proto'], [ROOT / 'otium', '--runtime=chez', '--no-project']]
outputs = []
for command in commands:
    result = subprocess.run([*map(str, command), '-L', 'tests/reader/data', 'tests/reader/tokens.ot'],
                            cwd=ROOT, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    outputs.append(result.stdout)
assert outputs[0] == outputs[1], 'token fixture output differs'
print('Chez token fixtures agree, including text, positions and newline gaps')
