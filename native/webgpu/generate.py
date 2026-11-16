#!/usr/bin/env python3
"""Generate lib/otium/webgpu/raw.ot, Otium bindings for webgpu.h and wgpu.h.

Declarations come from clang's JSON AST of the headers. A compiled probe
supplies what the AST doesn't: struct sizes and offsets (checked against the
layout rules otium.ffi uses), the bytes of every WGPU_*_INIT macro, and the
#define constants. Names come from webgpu.yml, which spells them the way the
WebGPU specification does (extent-3d, rgba8-unorm); wgpu.h's additions fall
back to splitting the C names.

Run it again after changing the wgpu-native version:
    native/webgpu/generate.py [--wgpu DIR] [--output FILE]
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]

SCALARS = {
    'unsigned char': 'u8', 'unsigned short': 'u16', 'unsigned int': 'u32', 'int': 'i32',
    'unsigned long': 'u64', 'unsigned long long': 'u64', 'long': 'i64', 'long long': 'i64',
    'float': 'float', 'double': 'double', '_Bool': 'bool',
}
SIZES = {'u8': 1, 'u16': 2, 'u32': 4, 'i32': 4, 'u64': 8, 'i64': 8, 'float': 4, 'double': 8,
         'bool': 1, 'size-t': 8, 'pointer': 8, 'callback-pointer': 8}
OT_INT_MAX = 2**62 - 1
# Calls that return quickly and never call back. The flag is advisory; see
# specs/ffi.md.
QUICK = re.compile(r'(AddRef|Release|Set[A-Z]\w*|Draw\w*|Dispatch\w*|Get\w*|Has\w*|'
                   r'Insert\w*|Push\w*|Pop\w*|End|Begin\w*Query)$')


def kebab(name):
    """Split CamelCase or snake_case into lower kebab-case."""
    name = re.sub(r'([a-z0-9])([A-Z])', r'\1-\2', name)
    name = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1-\2', name)
    return name.replace('_', '-').lower()


def pascal(snake):
    return ''.join(part[:1].upper() + part[1:] for part in snake.split('_'))


def camel(snake):
    p = pascal(snake)
    return p[:1].lower() + p[1:]


class Names:
    """C names to Otium names, preferring webgpu.yml's spelling."""

    def __init__(self, spec):
        self.types = {}
        for group in ('enums', 'bitflags', 'structs', 'callbacks', 'objects'):
            for item in spec[group]:
                self.types['WGPU' + pascal(item['name'])] = item['name'].replace('_', '-').lower()
        for item in spec['callbacks']:
            self.types['WGPU' + pascal(item['name']) + 'Callback'] = \
                item['name'].replace('_', '-').lower() + '-callback'
        self.entries = {}
        for group in ('enums', 'bitflags'):
            for item in spec[group]:
                for entry in item['entries']:
                    if entry:
                        self.entries[('WGPU' + pascal(item['name']), pascal(entry['name']))] = \
                            entry['name'].replace('_', '-').lower()
        self.members = {}
        for item in spec['structs']:
            for member in item.get('members', []):
                self.members[('WGPU' + pascal(item['name']), camel(member['name']))] = \
                    member['name'].replace('_', '-').lower()
        self.functions = {}
        for item in spec['functions']:
            self.functions['wgpu' + pascal(item['name'])] = item['name'].replace('_', '-').lower()
        for obj in spec['objects']:
            for method in obj.get('methods', []):
                self.functions['wgpu' + pascal(obj['name']) + pascal(method['name'])] = \
                    (obj['name'] + '-' + method['name']).replace('_', '-').lower()

    def type(self, c):
        return self.types.get(c) or kebab(c.removeprefix('WGPU'))

    def entry(self, enum, c_entry):
        suffix = c_entry.removeprefix(enum + '_')
        return self.entries.get((enum, suffix)) or kebab(suffix)

    def member(self, struct, field):
        return self.members.get((struct, field)) or kebab(field)

    def function(self, c):
        return self.functions.get(c) or kebab(c.removeprefix('wgpu'))


def ast(include, compiler):
    source = '#include <webgpu/webgpu.h>\n#include <webgpu/wgpu.h>\n'
    out = subprocess.run([*compiler, '-x', 'c', '-std=c11', '-I', str(include), '-fsyntax-only',
                          '-Xclang', '-ast-dump=json', '-'], input=source, text=True,
                         capture_output=True, check=True).stdout
    return json.loads(out)


def from_headers(node):
    loc = node.get('loc', {})
    file = loc.get('file') or loc.get('spellingLoc', {}).get('file') or ''
    return 'webgpu/' in file or file.endswith(('webgpu.h', 'wgpu.h'))


def collect(tree):
    """Declarations from the headers, in source order."""
    decls = {'typedef': {}, 'enum': {}, 'struct': {}, 'function': {}, 'flag': {}}
    file = None
    for node in tree['inner']:
        loc = node.get('loc', {})
        file = loc.get('file', file)
        if not file or 'webgpu/' not in file:
            continue
        kind, name = node['kind'], node.get('name')
        if kind == 'TypedefDecl':
            decls['typedef'][name] = node
        elif kind == 'EnumDecl' and name:
            decls['enum'][name] = node
        elif kind == 'RecordDecl' and name and node.get('completeDefinition'):
            decls['struct'][name] = node
        elif kind == 'FunctionDecl':
            decls['function'][name] = node
        elif kind == 'VarDecl' and node.get('storageClass') == 'static':
            decls['flag'][name] = node
    return decls


def integer(node):
    """The value of a constant initializer or enumerator."""
    for child in node.get('inner', []):
        if 'value' in child and child['kind'] in ('ConstantExpr', 'IntegerLiteral'):
            return int(child['value'])
        value = integer(child)
        if value is not None:
            return value
    return None


class Types:
    def __init__(self, decls, names):
        self.decls, self.names = decls, names
        self.callbacks = {name for name, node in decls['typedef'].items()
                          if '(*)' in node['type'].get('desugaredQualType', node['type']['qualType'])}
        self.flag_types = {name for name, node in decls['typedef'].items()
                           if node['type']['qualType'] == 'WGPUFlags'}
        # Anonymous unions inside structs, named <struct>_<field>.
        self.unions = set()
        for record, node in list(decls['struct'].items()):
            pending = None
            for child in node.get('inner', []):
                if child['kind'] == 'RecordDecl' and child.get('tagUsed') == 'union':
                    pending = child
                elif child['kind'] == 'FieldDecl' and pending is not None:
                    name = f"{record}_{child['name']}"
                    decls['struct'][name] = pending
                    self.unions.add(name)
                    child['type'] = {'qualType': name}
                    pending = None

    def resolve(self, qual, desugared=None):
        """An otium.ffi type for a C type: a scalar symbol or ('struct', C name)."""
        q = qual.replace('const ', '').replace(' const', '').strip()
        if q in self.callbacks:
            return 'callback-pointer'
        if '*' in q or q.endswith(']'):
            return 'pointer'
        if q == 'size_t':
            return 'size-t'
        if q in self.decls['struct'] or q.startswith('struct '):
            return ('struct', q.removeprefix('struct '))
        if q in self.decls['typedef']:
            node = self.decls['typedef'][q]
            inner = node['type'].get('desugaredQualType', node['type']['qualType'])
            if inner.startswith('struct ') and '*' not in inner:
                return ('struct', inner.removeprefix('struct '))
            if inner.startswith('enum '):
                return 'u32'
            return self.resolve(node['type']['qualType'], inner)
        if q.startswith('enum '):
            return 'u32'
        if q in SCALARS:
            return SCALARS[q]
        if desugared and desugared != qual:
            return self.resolve(desugared)
        if q in ('uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'int32_t', 'int64_t'):
            return {'uint8_t': 'u8', 'uint16_t': 'u16', 'uint32_t': 'u32', 'uint64_t': 'u64',
                    'int32_t': 'i32', 'int64_t': 'i64'}[q]
        raise SystemExit(f'Unknown C type: {qual!r} ({desugared!r})')

    def struct_name(self, record):
        """The typedef name of a struct record (WGPUColor for struct WGPUColor)."""
        return record

    def fields(self, record):
        node = self.decls['struct'][record]
        out = []
        for field in node.get('inner', []):
            if field['kind'] != 'FieldDecl':
                continue
            t = field['type']
            out.append((field['name'], self.resolve(t['qualType'], t.get('desugaredQualType'))))
        return out

    def kind(self, qual, names):
        """How the Otium layer converts a value for a field of C type qual, as
        Otium source, or None for a plain scalar."""
        q = qual.replace('const ', '').replace(' const', '').strip()
        t = self.resolve(qual)
        if t == ('struct', 'WGPUStringView'):
            return "(list :string)"
        if isinstance(t, tuple):
            return f"(list :struct :{names.type(t[1])})"
        if t == 'callback-pointer':
            return None
        if q == 'WGPUBool':
            return "(list :bool)"
        if q in self.flag_types:
            return f"(list :flags {names.type(q)})"
        if q in self.decls['enum'] or self.typedef_kind(q) == 'enum':
            return f"(list :enum {names.type(q)})"
        return None

    def typedef_kind(self, q):
        node = self.decls['typedef'].get(q)
        if not node:
            return None
        inner = node['type'].get('desugaredQualType', node['type']['qualType'])
        if inner.startswith('enum '):
            return 'enum'
        if inner.startswith('struct ') and inner.endswith('*'):
            return 'object'
        return None

    def element(self, pointee, names):
        """The array element kind for a pointer field's pointee type."""
        t = self.resolve(pointee) if pointee not in ('void', 'char') else None
        if isinstance(t, tuple):
            return f":{names.type(t[1])}"
        if self.typedef_kind(pointee) == 'object':
            return "'pointer"
        if pointee in self.decls['enum'] or self.typedef_kind(pointee) == 'enum':
            return "'u32"
        return f"'{t}" if t else None

    def field_kinds(self, record, names):
        node = self.decls['struct'][record]
        fields = [f for f in node.get('inner', []) if f['kind'] == 'FieldDecl']
        out = []
        for i, field in enumerate(fields):
            qual = field['type']['qualType']
            if '*' in qual and record not in self.unions:
                pointee = qual.replace('const', '').replace('*', '').strip()
                previous = fields[i - 1]['name'] if i else ''
                element = self.element(pointee, names)
                if previous.endswith('Count') and element:
                    count = names.member(record, previous)
                    out.append((field['name'], f"(list :array {element} :{count})"))
                elif element and element.startswith(':'):
                    out.append((field['name'], f"(list :pointer {element})"))
                continue
            kind = self.kind(qual, names)
            if kind:
                out.append((field['name'], kind))
        return out

    def function(self, name):
        node = self.decls['function'][name]
        ret = node['type']['qualType'].split('(')[0].strip()
        args = []
        for p in node.get('inner', []):
            if p['kind'] == 'ParmVarDecl':
                t = p['type']
                args.append(self.resolve(t['qualType'], t.get('desugaredQualType')))
        result = 'void' if ret == 'void' else self.resolve(ret)
        return args, result

    def callback(self, name):
        """A callback's argument types and result, with by-value string views
        split into their two words: on x86-64 and arm64 a 16-byte struct of
        two integers travels in two registers, like two arguments."""
        node = self.decls['typedef'][name]
        sig = node['type'].get('desugaredQualType', node['type']['qualType'])
        ret, params = re.match(r'(.*)\(\*\)\((.*)\)', sig).groups()
        args = []
        for p in [p.strip() for p in params.split(',') if p.strip() and p.strip() != 'void']:
            t = self.resolve(p)
            if t == ('struct', 'WGPUStringView'):
                args += ['pointer', 'size-t']
            elif isinstance(t, tuple):
                raise SystemExit(f'{name}: by-value struct argument {p}')
            else:
                args.append(t)
        ret = ret.strip()
        return args, 'void' if ret == 'void' else self.resolve(ret)


def layout(types, record, memo):
    """Size, alignment and field offsets by the rules otium.ffi applies."""
    if record in memo:
        return memo[record]
    size, align, offsets = 0, 1, []
    union = record in types.unions
    for name, t in types.fields(record):
        fs, fa = (layout(types, t[1], memo)[:2] if isinstance(t, tuple) else (SIZES[t], SIZES[t]))
        offset = 0 if union else (size + fa - 1) // fa * fa
        offsets.append(offset)
        size = max(size, offset + fs)
        align = max(align, fa)
    memo[record] = ((size + align - 1) // align * align, align, offsets)
    return memo[record]


def probe(include, compiler, structs, types, constants):
    lines = ['#include <stddef.h>', '#include <stdio.h>', '#include <stdint.h>',
             '#include <webgpu/webgpu.h>', '#include <webgpu/wgpu.h>',
             'static void bytes(const char* name, const void* p, size_t n) {',
             '  printf("init %s", name);',
             '  for (size_t i = 0; i < n; ++i) printf(" %u", ((const unsigned char*)p)[i]);',
             '  printf("\\n");', '}', 'int main(void) {']
    for record in structs:
        lines.append(f'printf("layout {record} %zu %zu\\n", sizeof({record}), _Alignof({record}));')
        for field, _ in types.fields(record):
            lines.append(f'printf("field {record} {field} %zu\\n", offsetof({record}, {field}));')
        macro = 'WGPU_' + re.sub(r'([a-z0-9])([A-Z])', r'\1_\2',
                                 re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1_\2',
                                        record.removeprefix('WGPU'))).upper() + '_INIT'
        lines += [f'#ifdef {macro}',
                  f'{{ {record} v = {macro}; bytes("{record}", &v, sizeof v); }}',
                  '#endif']
    for name in constants:
        lines += [f'#ifdef {name}',
                  f'printf("constant {name} %.17g %llu\\n", (double)({name}), '
                  f'(unsigned long long)({name}));', '#endif']
    lines += ['return 0;', '}']
    with tempfile.TemporaryDirectory() as tmp:
        src, exe = Path(tmp) / 'probe.c', Path(tmp) / 'probe'
        src.write_text('\n'.join(lines) + '\n')
        subprocess.run([*compiler, '-std=c11', '-w', '-I', str(include), str(src), '-o', str(exe)],
                       check=True)
        return subprocess.run([str(exe)], text=True, capture_output=True, check=True).stdout


def otium_type(t, names):
    return names.type(t[1]) if isinstance(t, tuple) else f"'{t}"


def generate(wgpu, spec_path, compiler):
    include = wgpu / 'include'
    spec = yaml.safe_load(spec_path.read_text())
    names = Names(spec)
    decls = collect(ast(include, compiler))
    types = Types(decls, names)
    version = (wgpu / 'wgpu-native-meta' / 'wgpu-native-git-tag').read_text().strip()

    structs = [r for r in decls['struct']
               if r.startswith('WGPU') and not r.endswith('Impl') and r not in types.unions]
    constants = ['WGPU_' + c['name'].upper() for c in spec['constants']]
    report = probe(include, compiler, structs, types, constants)
    measured, inits, values = {}, {}, {}
    for line in report.splitlines():
        kind, name, *rest = line.split()
        if kind == 'layout':
            measured.setdefault(name, {})['size'] = (int(rest[0]), int(rest[1]))
        elif kind == 'field':
            measured[name].setdefault('fields', []).append(int(rest[1]))
        elif kind == 'init':
            inits[name] = [int(b) for b in rest]
        elif kind == 'constant':
            values[name] = (rest[0], int(rest[1]))
    memo = {}
    for record in structs:
        size, align, offsets = layout(types, record, memo)
        m = measured[record]
        if (size, align) != m['size'] or offsets != m.get('fields', []):
            raise SystemExit(f'Layout differs for {record}: {(size, align, offsets)} vs {m}')

    out = [f';; Generated by native/webgpu/generate.py from webgpu.h and wgpu.h',
           f';; (wgpu-native {version}). Do not edit; run the generator again instead.',
           '(ns otium.webgpu.raw)',
           '(require (otium.ffi :as ffi))',
           '',
           ';; Constants that fit an Otium integer. The u64 all-ones sentinels',
           ';; (whole-size and the like) do not, and come from defaults instead.',
           '(def constants',
           '  {']
    for c in spec['constants']:
        macro = 'WGPU_' + c['name'].upper()
        if macro not in values:
            continue
        text, unsigned = values[macro]
        key = ':' + c['name'].replace('_', '-').lower()
        if text != 'nan' and unsigned <= OT_INT_MAX and float(text) == unsigned:
            out.append(f'   {key} {unsigned}')
    out.append('  })')

    out += ['', ';; Enums and bitflags, as tables from keyword to value.']
    flags = {}
    for name, node in decls['flag'].items():
        prefix = name.split('_')[0]
        value = integer(node)
        if value is not None:
            flags.setdefault(prefix, []).append((name, value))
    for enum, node in decls['enum'].items():
        entries = [(e['name'], integer(e)) for e in node.get('inner', [])
                   if e['kind'] == 'EnumConstantDecl' and not e['name'].endswith('_Force32')]
        if enum.startswith('WGPU') and entries:
            emit_table(out, names.type(enum), enum, entries, names)
    for prefix, entries in flags.items():
        emit_table(out, names.type(prefix), prefix, entries, names)

    out += ['', ';; Struct layouts, in dependency order.']
    done = set()

    def emit(record):
        if record in done:
            return
        done.add(record)
        fields = types.fields(record)
        for _, t in fields:
            if isinstance(t, tuple):
                emit(t[1])
        parts = ' '.join(f"(list '{names.member(record, f)} {otium_type(t, names)})"
                         for f, t in fields)
        define = 'define-union!' if record in types.unions else 'define-struct!'
        out.append(f"(def {names.type(record)} (ffi/{define} '{names.type(record)} "
                   f'(list {parts})))')
        out.append('')
    for record in structs:
        emit(record)

    out += [';; Struct layouts by name.', '(def layouts', '  {']
    for record in structs:
        out.append(f'   :{names.type(record)} {names.type(record)}')
    out += ['  })', '',
            ';; How otium.webgpu converts Otium values for each struct field that is',
            ';; not a plain number: (:string), (:bool), (:enum table), (:flags table),',
            ';; (:struct name), (:pointer name) for one struct behind a pointer, or',
            ';; (:array element count-field), element being a struct name or scalar type.',
            '(def fields', '  {']
    for record in structs:
        kinds = types.field_kinds(record, names)
        if kinds:
            body = ' '.join(f':{names.member(record, f)} {k}' for f, k in kinds)
            out.append(f'   :{names.type(record)} {{{body}}}')
    out += ['  })', '']
    out += [';; The nonzero bytes of each WGPU_*_INIT, as (offset byte) pairs. A',
            ';; zeroed struct with these bytes set has the C defaults.',
            '(def defaults', '  {']
    for record in structs:
        if any(inits.get(record, [])):
            pairs = ' '.join(f'({i} {b})' for i, b in enumerate(inits[record]) if b)
            out.append(f"   :{names.type(record)} '({pairs})")
    out.append('  })')

    out += ['', ';; Callback signatures for ffi/callback: (arguments result). String',
            ';; views arrive as their data pointer and length.', '(def callbacks', '  {']
    for name in sorted(types.callbacks):
        if not name.startswith('WGPU') or name.startswith('WGPUProc'):
            continue
        args, result = types.callback(name)
        arglist = ' '.join(args)
        out.append(f"   :{names.type(name)} '(({arglist}) {result})")
    out.append('  })')

    out += ['', ';; Functions. Each is nil until load! binds it.', '(defonce library nil)', '']
    functions = [f for f in decls['function'] if f.startswith('wgpu') and f in decls['function']]
    bound = []
    for f in functions:
        args, result = types.function(f)
        name = names.function(f)
        bound.append((name, f, args, result))
        out.append(f'(def {name} nil)')
    out += ['', '(def- (bind-all!)']
    for name, f, args, result in bound:
        arglist = ' '.join(otium_type(a, names) for a in args)
        res = otium_type(result, names) if result != 'void' else "'void"
        quick = '#f' if QUICK.search(f) else '#t'
        out.append(f'  (set! {name} (ffi/bind library "{f}" (list {arglist}) {res} {quick}))')
    out[-1] += ')'
    out += ['',
            '(def (load! names)',
            '  "Open the wgpu-native library, a path or list of candidates, and bind every function."',
            '  (set! library (ffi/open-library (if (string? names) (list names) names)))',
            '  (bind-all!))',
            '',
            ';; Reloading this module keeps the library and binds again.',
            '(when library (bind-all!))', '']
    return '\n'.join(out)


def emit_table(out, name, prefix, entries, names):
    if not entries:
        return
    out.append(f'(def {name}')
    body = ' '.join(f':{names.entry(prefix, e)} {v}' for e, v in entries if v is not None)
    out.append(f'  {{{body}}})')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    default = next(iter(sorted((ROOT / 'subprojects').glob('wgpu-native-*'))), None)
    parser.add_argument('--wgpu', type=Path, default=default, help='an unpacked wgpu-native release')
    parser.add_argument('--output', type=Path, default=ROOT / 'lib/otium/webgpu/raw.ot')
    args = parser.parse_args()
    if not args.wgpu or not (args.wgpu / 'include/webgpu/webgpu.h').exists():
        raise SystemExit('No wgpu-native release; configure the build with -Dwebgpu=enabled first')
    compiler = shlex.split(os.environ.get('CC', 'clang'))
    text = generate(args.wgpu, args.wgpu / 'wgpu-native-meta/webgpu.yml', compiler)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    # The formatter needs a build; without one the output is valid but unformatted.
    if os.environ.get('OTIUM_BUILD'):
        subprocess.run([str(ROOT / 'otium'), 'fmt', str(args.output)], check=True)
    else:
        print('OTIUM_BUILD is not set, so the output is not formatted')
    print(f'wrote {args.output.relative_to(ROOT)}')


if __name__ == '__main__':
    main()
