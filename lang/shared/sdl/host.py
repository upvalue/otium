"""Loader adapters; SDL declarations and application logic are shared Scheme."""
import json
import os
from pathlib import Path
import platform
from schema import STRUCTS, MEMBERS, TYPEDEFS
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
TARGET = platform.machine() + '-' + platform.system().lower()
DEFAULT_MANIFEST = HERE / 'manifests' / (TARGET + '.json')

def literal(value):
    return json.dumps(str(value), ensure_ascii=False)

def bootstrap(runtime, manifest=DEFAULT_MANIFEST):
    data = json.loads(Path(manifest).read_text())
    if data['schema'] != 1 or data['target'] != TARGET or TARGET not in ('arm64-darwin','x86_64-linux'):
        raise ValueError('Unsupported SDL ABI manifest target/schema; run shared/sdl/setup.py')
    for name, item in data['typedef'].items():
        if item['representation'] != TYPEDEFS[name] or item['signed'] != (TYPEDEFS[name] in ('int','i32')):
            raise ValueError('Unsupported SDL typedef representation: ' + name)
    override = os.environ.get('SDL3_LIBRARY')
    candidates = [override] if override is not None else ([data['library']] if data.get('library') else (['libSDL3.dylib'] if platform.system() == 'Darwin' else ['libSDL3.so.0','libSDL3.so']))
    prefix = "(require '(otium.ffi :as ffi))\n" if runtime == 'c' else '(require otium.ffi :as ffi)\n'
    if runtime == 'c':
        prefix += '(def (sdl-abi-error message) (error (table :type \'ffi-error :reason \'abi :operation \'sdl-layout :message message)))\n(def host-read read-string)\n'
    else:
        prefix += '''(def (sdl-abi-error message)
 (let ((c (make-eq-hashtable)))
  (hashtable-set! c (string->symbol ":type") 'ffi-error) (hashtable-set! c (string->symbol ":reason") 'abi)
  (hashtable-set! c (string->symbol ":operation") 'sdl-layout) (hashtable-set! c (string->symbol ":message") message) (error c)))
(def host-read read-string)
'''
    prefix += "(def sdl-library-names '(" + ' '.join(map(literal,candidates)) + '))\n'
    prefix += '\n'.join(f'(def {name} {value})' for name,value in data['constant'].items()) + '\n'
    checks = []
    for name,item in data['scalar'].items():
        checks += [f"(sdl-layout-check {item['size']} (ffi/sizeof '{name}))", f"(sdl-layout-check {item['align']} (ffi/alignof '{name}))"]
    for name,item in data['typedef'].items():
        checks += [f"(sdl-layout-check {item['size']} (ffi/sizeof '{item['representation']}))", f"(sdl-layout-check {item['align']} (ffi/alignof '{item['representation']}))"]
    for name,item in data['layout'].items():
        checks += [f"(sdl-layout-check {item['size']} (ffi/sizeof {name}))", f"(sdl-layout-check {item['align']} (ffi/alignof {name}))"]
        checks += [f"(sdl-layout-check {offset} (ffi/offset-of {name} '({field})))" for field,offset in item['fields'].items()]
    prefix += '(def (sdl-layout-check expected actual) (if (= expected actual) #t (sdl-abi-error "SDL header layout mismatch")))\n'
    prefix += '(def (sdl-verify-layouts!)\n' + '\n'.join(checks) + ')\n'
    math_names = '("/usr/lib/libSystem.B.dylib")' if platform.system() == 'Darwin' else '("libm.so.6" "libm.so")'
    prefix += "(def sdl-math (ffi/open-library '" + math_names + "))\n"
    prefix += "(def sin (ffi/bind sdl-math \"sin\" '(double) 'double))\n(def cos (ffi/bind sdl-math \"cos\" '(double) 'double))\n"
    return prefix + (HERE/'bindings.scm').read_text() + (HERE/'canvas.scm').read_text()

def command(runtime, script):
    return ([str(ROOT/'c/otium'),'--no-project'] if runtime == 'c' else [os.environ.get('CHEZ','chez'),'--libdirs',str(ROOT/'chez'),'--script',str(ROOT/'chez/main.ss')]) + [str(script)]

def environment():
    extension = 'dylib' if platform.system() == 'Darwin' else 'so'
    return dict(os.environ, OTIUM_FFI_LIBRARY=str(ROOT/'shared/build'/TARGET/('libotium-ffi.'+extension)))
