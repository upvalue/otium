"""Write native installation paths as Otium data."""
import json
from pathlib import Path
import sys

output, library, events, sdl = sys.argv[1:]
output = Path(output)
output.mkdir(parents=True, exist_ok=True)
for name, entries in (
    ('ui-config.ot', (('library', library), ('sdl', sdl))),
    ('ui-test-config.ot', (('library', events),)),
):
    text = '(' + ' '.join(f'({key} {json.dumps(value)})' for key, value in entries) + ')\n'
    path = output / name
    if not path.exists() or path.read_text() != text:
        temporary = path.with_suffix('.tmp')
        temporary.write_text(text)
        temporary.replace(path)
