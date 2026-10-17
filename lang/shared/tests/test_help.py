#!/usr/bin/env python3
"""Check the shared reference and source docstrings without fetching manuals."""
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[2]
subprocess.run([sys.executable, str(ROOT / 'shared/documentation/generate.py'), '--check'], check=True)
source = (ROOT / 'c/src/otium.c').read_text()
natives = source.split('static const nat_def core_nats[] = {')[1].split('};')[0]
specials = source.split('static bool special_name(')[1].split('};')[0]
required = set(re.findall(r'\{"([^"]+)"', natives))
required.update(re.findall(r'^\s*"([^"]+)"', specials, re.M))
entries = json.loads((ROOT / 'shared/documentation/runtime.json').read_text())
assert not required - entries.keys(), sorted(required - entries.keys())
prelude = (ROOT / 'c/prelude/prelude.scm').read_text()
for match in re.finditer(r'^\((?:define\s+\(([^\s()]+)[^\n]*?\)|defmacro\s+([^\s()]+)\s+\([^\n]*?\))', prelude, re.M):
    name = match[1] or match[2]
    assert prelude[match.end():].lstrip().startswith('"'), f'Missing source docstring: {name}'
    assert 'c' not in entries.get(name, {}).get('runtimes', []), f'Duplicate C documentation: {name}'
print('Runtime reference coverage, generated files, and prelude docstrings passed')
