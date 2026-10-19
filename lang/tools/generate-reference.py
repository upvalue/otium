#!/usr/bin/env python3
"""Compile the shared Otium runtime reference; no network access required."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
entries = json.loads((ROOT / 'lib/catalog/runtime.json').read_text())

def pairs(runtime):
    return [(name, entry.get(runtime, entry['text'])) for name, entry in entries.items()
            if runtime in entry['runtimes']]

def quote(text):
    return json.dumps(text, ensure_ascii=False)

c = ['/* Generated from lib/catalog/runtime.json. */',
     'const char* ot_runtime_documentation(const char* name) {',
     '  static const struct { const char* name; const char* text; } entries[] = {']
c += ['    {' + quote(name) + ', ' + quote(text) + '},' for name, text in pairs('c')]
c += ['  };', '  for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); ++i)',
      '    if (!strcmp(name, entries[i].name)) return entries[i].text;', '  return "";', '}','']
scm = [';; Generated from lib/catalog/runtime.json.',
       '(library (otium runtime-documentation)', '  (export runtime-documentation)',
       '  (import (chezscheme))', '  (define (runtime-documentation name)',
       '    (let ([entry (assq name entries)]) (if entry (cdr entry) "")))',
       "  (define entries '(" ]
scm += ['    ('+ name +' . '+quote(text)+')' for name,text in pairs('chez')]
scm += ['  )))','']
for name, text in [('c/src/runtime-reference.inc','\n'.join(c)),
                   ('chez/otium/runtime-documentation.sls','\n'.join(scm))]:
    path=ROOT/name
    if args.check:
        assert path.read_text() == text, f'Regenerate {name}: python3 tools/generate-reference.py'
    else: path.write_text(text)
