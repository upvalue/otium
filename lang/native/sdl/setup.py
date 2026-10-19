#!/usr/bin/env python3
"""Generate an SDL ABI manifest with a native header probe. No runtime wrappers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import tempfile
import sys

# Repository modules, for running this file directly.
sys.path[:0] = [str(Path(__file__).resolve().parents[2] / part) for part in ('', 'tools')]
from native.sdl.schema import STRUCTS, SCALARS, TYPEDEFS, CONSTANTS, MEMBERS

HERE = Path(__file__).resolve().parent
TARGET = platform.machine() + '-' + platform.system().lower()
DEFAULT = HERE / 'manifests' / (TARGET + '.json')

def output(*command):
    return subprocess.check_output(command, text=True).strip()

def probe_source():
    lines = ['#include <SDL3/SDL.h>', '#include <stdio.h>', '#include <stddef.h>',
             '#define REP(x) _Generic((x), bool: "bool", signed char: "i8", unsigned char: "u8", short: "i16", unsigned short: "u16", int: "i32", unsigned int: "u32", long: sizeof(long) == 8 ? "i64" : "i32", unsigned long: sizeof(long) == 8 ? "u64" : "u32", long long: "i64", unsigned long long: "u64", float: "float", double: "double", char *: "pointer", const char *: "pointer", default: "aggregate")',
             'int main(void) {']
    lines += ['printf("version %d\\n", SDL_VERSION);']
    for group, entries in [('scalar', SCALARS), ('typedef', TYPEDEFS)]:
        for name, value in entries.items():
            ctype = value if group == 'scalar' else name
            lines += [f'printf("{group} {name} %zu %zu\\n", sizeof({ctype}), _Alignof({ctype}));']
            if group == 'typedef':
                lines += [f'printf("signed {name} %d\\n", _Generic(({ctype})0, unsigned char: 0, unsigned short: 0, unsigned int: 0, unsigned long: 0, unsigned long long: 0, default: 1));']
    event_fields = [('type', 'u32'), *MEMBERS.items(), ('padding', 'u8')]
    for name, fields in {**STRUCTS, 'SDL_Event': event_fields}.items():
        lines += [f'printf("layout {name} %zu %zu\\n", sizeof({name}), _Alignof({name}));']
        for field, _ in fields:
            lines += [f'printf("field {name} {field} %zu\\n", offsetof({name}, {field}));']
            if name in STRUCTS:
                lines += [f'printf("field-type {name} {field} %zu %zu %s\\n", sizeof((({name} *)0)->{field}), _Alignof(__typeof__((({name} *)0)->{field})), REP((({name} *)0)->{field}));']
    for name in CONSTANTS:
        lines += [f'printf("constant {name} %llu\\n", (unsigned long long){name});']
    return '\n'.join(lines + ['return 0;', '}'])

def generate():
    if TARGET not in ('arm64-darwin', 'x86_64-linux'):
        raise SystemExit('Unsupported SDL manifest target: ' + TARGET)
    cc = shlex.split(os.environ.get('CC', 'cc'))
    options = shlex.split(os.environ.get('CFLAGS', ''))
    flags = shlex.split(output('pkg-config', '--cflags', 'sdl3'))
    build_options = options + ['-std=c11', '-Wall', '-Wextra', '-Werror'] + flags
    include = Path(output('pkg-config', '--variable=includedir', 'sdl3')) / 'SDL3'
    digest = hashlib.sha256()
    for path in sorted(include.glob('*.h')):
        digest.update(path.name.encode()); digest.update(path.read_bytes())

    if any(flag in ('-target', '--target', '-arch') or flag.startswith(('--target=', '-target=', '-arch=', '-m32')) for flag in cc[1:] + options + flags):
        raise SystemExit('Explicit compiler target overrides are unsupported')
    triple = output(*cc, '-dumpmachine')
    architecture_ok = any(arch in triple for arch in ('arm64', 'aarch64')) if TARGET.startswith('arm64') else 'x86_64' in triple
    system_ok = ('darwin' in triple) if TARGET.endswith('darwin') else ('linux' in triple)
    if not architecture_ok or not system_ok:
        raise SystemExit('Cross compilation is unsupported; compiler and host target differ')
    data = dict(schema=1, target=TARGET, target_triple=triple, compiler=output(*cc, '--version').splitlines()[0], compiler_command=cc, compiler_abi_options=build_options, baseline=3004012, header_sha256=digest.hexdigest(), pkg_config_version=output('pkg-config','--modversion','sdl3'), scalar={}, typedef={}, layout={}, constant={})
    with tempfile.TemporaryDirectory(prefix='otium-sdl-probe-') as temp:
        source = Path(temp)/'probe.c'; executable = Path(temp)/'probe'
        source.write_text(probe_source())
        subprocess.run(cc + build_options + [str(source), '-o', str(executable)], check=True)
        for line in output(str(executable)).splitlines():
            parts = line.split(); kind = parts[0]
            if kind == 'version': data['header_version'] = int(parts[1])
            elif kind == 'constant': data[kind][parts[1]] = int(parts[2])
            elif kind == 'signed': data['typedef'][parts[1]]['signed'] = bool(int(parts[2]))
            elif kind == 'field-type':
                data['layout'][parts[1]]['field_types'][parts[2]] = dict(size=int(parts[3]), align=int(parts[4]), representation=parts[5])
            elif kind == 'field': data['layout'][parts[1]]['fields'][parts[2]] = int(parts[3])
            else:
                data[kind][parts[1]] = dict(size=int(parts[2]), align=int(parts[3]))
                if kind == 'layout':
                    data[kind][parts[1]]['fields'] = {}
                    data[kind][parts[1]]['field_types'] = {}
                if kind == 'typedef': data[kind][parts[1]]['representation'] = TYPEDEFS[parts[1]]
    if data['header_version'] < data['baseline'] or data['header_version']//1000000 != 3:
        raise SystemExit('SDL 3 headers >= 3.4.12 required')
    for name, fields in STRUCTS.items():
        for field, expected in fields:
            actual = data['layout'][name]['field_types'][field]
            scalar = data['scalar'][expected]
            representation = 'i32' if expected == 'int' else expected
            if actual != dict(size=scalar['size'], align=scalar['align'], representation=representation):
                raise SystemExit(f'Unsupported SDL field representation: {name}.{field}: {actual}')
    for name, expected in TYPEDEFS.items():
        actual = data['typedef'][name]
        scalar = data['scalar'][expected]
        if (actual['size'], actual['align'], actual['signed']) != (scalar['size'], scalar['align'], expected in ('int','i32')):
            raise SystemExit(f'Unsupported SDL typedef representation: {name}: {actual}')
    libdir = Path(output('pkg-config','--variable=libdir','sdl3'))
    data['library'] = str(libdir / ('libSDL3.dylib' if platform.system() == 'Darwin' else 'libSDL3.so'))
    return data

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=DEFAULT)
    parser.add_argument('--check', action='store_true', help='Regenerate and compare with the installed manifest')
    args = parser.parse_args(); data = generate()
    if args.check:
        if json.loads(args.manifest.read_text()) != data: raise SystemExit('SDL manifest differs from installed headers/compiler; run setup.py')
        print('SDL manifest matches headers')
    else:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps(data, indent=2, sort_keys=True)+'\n')
        print(f'SDL {data["pkg_config_version"]} manifest: {args.manifest} (runtime baseline 3.4.12)')
