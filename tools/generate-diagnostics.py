#!/usr/bin/env python3
"""Compile lib/catalog/diagnostics.json into runtime tables, an Otium module and docs."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
entries = json.loads((ROOT / 'lib/catalog/diagnostics.json').read_text())
errors = {code: entry for code, entry in entries.items() if code.startswith('E')}
rules = {code: entry for code, entry in entries.items() if code.startswith('L')}
SOURCE = 'Generated from lib/catalog/diagnostics.json by tools/generate-diagnostics.py.'


def c_string(text):
    return json.dumps(text, ensure_ascii=False)


def otium_string(text):
    escaped = (text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')
               .replace('\t', '\\t'))
    return f'"{escaped}"'


c = [f'/* {SOURCE} */',
     'static const struct { const char* code; const char* type; const char* message; }',
     '    ot_diagnostic_entries[] = {']
c += [f'  {{{c_string(code)}, {c_string(e["type"])}, {c_string(e["message"])}}},'
      for code, e in errors.items()]
c += ['};', '']

scm = [f';; {SOURCE}',
       '(library (otium diagnostic-catalog)',
       '  (export diagnostic-entry)',
       '  (import (chezscheme))',
       '  ;; (type . message template) for a code string, or #f.',
       '  (define (diagnostic-entry code)',
       '    (let ([entry (assoc code entries)]) (and entry (cdr entry))))',
       "  (define entries '("]
scm += [f'    ({c_string(code)} {e["type"]} . {c_string(e["message"])})'
        for code, e in errors.items()]
scm += ['  )))', '']

FIELDS = ['title', 'type', 'name', 'group', 'level', 'message', 'explanation', 'example', 'fixed']
# Laid out as otium fmt lays it out, so the format check passes.
ot = [f';; {SOURCE}', '(ns otium.diagnostics.catalog)', '',
      '(def entries', '  "Diagnostic code strings mapped to tables of :title, :message, :explanation,',
      '  :example and :fixed. Error codes have a :type; lint codes have a :name, :group and :level."']
for index, (code, e) in enumerate(entries.items()):
    fields = []
    for field in FIELDS:
        if field not in e:
            continue
        value = e[field]
        if field in ('type', 'group', 'level'):
            fields.append(f':{field} :{value}')
        else:
            fields.append(f':{field} {otium_string(value)}')
    prefix = '  {' if index == 0 else '   '
    ot.append(f'{prefix}{otium_string(code)} {{{fields[0]}')
    ot += [' ' * (len(prefix) + len(code) + 4) + field for field in fields[1:]]
    ot[-1] += '}'
ot[-1] += '})'
ot.append('')


def markdown(title, intro, selection):
    lines = [f'# {title}', '', f'<!-- {SOURCE} -->', '', intro, '']
    for code, e in selection.items():
        heading = f'{code} {e["name"]}' if 'name' in e else code
        lines += [f'## {heading}: {e["title"]}', '']
        if 'level' in e:
            lines += [f'Group `{e["group"]}`, default level `{e["level"]}`.', '']
        else:
            lines += [f'Condition type `:{e["type"]}`.', '']
        lines += [e['explanation'], '']
        if e.get('example'):
            lines += ['Example:', '', '```lisp', e['example'], '```', '']
        if e.get('fixed'):
            lines += ['Corrected:', '', '```lisp', e['fixed'], '```', '']
    return '\n'.join(lines)


errors_md = markdown(
    'Error codes',
    'Errors raised by the reader, the expander and the runtime carry a stable code in their '
    '`:code` field. Codes are never reused. Run `otium explain CODE` to print an entry.',
    errors)
lint_md = markdown(
    'Lint rules',
    'Rules checked by `otium lint`. Suppress one for a form with a comment on the line before it '
    'or at the end of its first line, `;; lint: allow(rule-name) reason`, or for a whole file '
    'with `;; lint: allow-file(rule-name) reason` before the first form. A reason is required. '
    'Run `otium explain CODE` to print an entry.',
    rules)

outputs = [('c/src/diagnostic-catalog.inc', '\n'.join(c)),
           ('chez/otium/diagnostic-catalog.sls', '\n'.join(scm)),
           ('lib/otium/diagnostics/catalog.ot', '\n'.join(ot)),
           ('docs/errors.md', errors_md),
           ('docs/lint.md', lint_md)]
for name, text in outputs:
    path = ROOT / name
    if args.check:
        assert path.exists() and path.read_text() == text, \
            f'Regenerate {name}: python3 tools/generate-diagnostics.py'
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
