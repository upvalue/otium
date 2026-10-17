"""✨ Check generated links and that reference entries follow their source docstrings."""
from html.parser import HTMLParser
from pathlib import Path
import sys
from urllib.parse import unquote, urlsplit
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from reference import entries

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'site/dist'


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if 'id' in values:
            assert values['id'] not in self.ids, values['id']
            self.ids.add(values['id'])
        for attr in ('href', 'src'):
            if attr in values:
                self.links.append(values[attr])


pages = {}
for path in OUT.rglob('*.html'):
    parser = Links()
    parser.feed(path.read_text())
    pages[path] = parser
    assert '✨' in path.read_text(), path
for path, page in pages.items():
    for link in page.links:
        parsed = urlsplit(link)
        if parsed.scheme or parsed.netloc:
            continue
        assert not parsed.path.startswith('/'), (path, link)
        target = (path.parent / unquote(parsed.path)).resolve() if parsed.path else path
        assert target.exists(), (path, link)
        if parsed.fragment and target in pages:
            assert unquote(parsed.fragment) in pages[target].ids, (path, link)
reference = entries()
assert len(reference) > 150
assert reference['map']['source'] == 'shared/prelude/core.ot'
assert 'Kind-preserving' in reference['map']['text']
assert 'to-list' not in reference
assert reference['+']['source'] == 'shared/documentation/runtime.json'
assert (OUT / 'assets/otium.wasm').stat().st_size > 10000
print(f'Site: {len(pages)} pages, local links, anchors, and {len(reference)} reference entries passed.')
