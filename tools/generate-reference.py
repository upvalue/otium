#!/usr/bin/env python3
"""Compile the Otium runtime reference; no network access required."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
entries = json.loads((ROOT / 'lib/catalog/runtime.json').read_text())

def quote(text):
    return json.dumps(text, ensure_ascii=False)

c = ['/* Generated from lib/catalog/runtime.json. */',
     'static const char* oti_runtime_documentation(const char* name) {',
     '  static const struct { const char* name; const char* text; } entries[] = {']
c += ['    {' + quote(name) + ', ' + quote(text) + '},' for name, text in entries.items()]
c += ['  };', '  for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); ++i)',
      '    if (!strcmp(name, entries[i].name)) return entries[i].text;', '  return "";', '}','']
name = 'c/src/runtime-reference.inc'
path = ROOT / name
text = '\n'.join(c)
if args.check:
    assert path.read_text() == text, f'Regenerate {name}: python3 tools/generate-reference.py'
else:
    path.write_text(text)
