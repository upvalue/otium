"""✨ Collect reference text from the shared runtime catalog and prelude docstrings."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def entries():
    catalog = json.loads((ROOT / 'shared/documentation/runtime.json').read_text())
    result = {name: {'text': entry.get('c', entry['text']), 'source': 'shared/documentation/runtime.json'}
              for name, entry in catalog.items() if 'c' in entry['runtimes']}
    source = (ROOT / 'c/prelude/prelude.scm').read_text()
    pattern = r'^\((?:def\s+\(([^\s()]+)([^)]*)\)|defmacro\s+([^\s()]+)\s+\(([^)]*)\))\s*\n?\s*("(?:[^"\\]|\\.)*")'
    for match in re.finditer(pattern, source, re.MULTILINE):
        name = match[1] or match[3]
        text = json.loads(match[5])
        if not text.startswith('('):
            text = '(' + name + (match[2] or match[4] or '') + ')\n' + text
        result[name] = {'text': text, 'source': 'c/prelude/prelude.scm'}
    return dict(sorted(result.items()))


def markdown():
    lines = ['# Language reference', '', '> ✨ Generated from runtime reference text and C prelude docstrings. Review before relying on it.', '',
             'This reference describes the C dialect used in the [playground](playground.md). '
             'Definition docstrings take precedence over the shared runtime catalog, as they do in `describe`. '
             'Native-only facilities may be documented here but are unavailable in the browser.', '',
             '<label for="reference-search">Find a binding</label>',
             '<input id="reference-search" type="search" placeholder="map, table, restart…" autocomplete="off">',
             '<p id="reference-count" role="status"></p>', '']
    for index, (name, entry) in enumerate(entries().items()):
        signature, _, body = entry['text'].partition('\n')
        lines += [f'<section class="reference-entry" id="binding-{index}">', '',
                  f'## `{name}`', '', '```scheme', signature, '```', '', body, '',
                  f'Source: `{entry["source"]}`', '', '</section>', '']
    return '\n'.join(lines)


if __name__ == '__main__':
    text = markdown()
    for folder in (ROOT / 'site/.generated', ROOT / 'site/public'):
        folder.mkdir(parents=True, exist_ok=True)
        (folder / 'reference.md').write_text(text)
