project('otium-web', 'c',
  version: '0.1.0',
  meson_version: '>= 1.6',
  default_options: [
    'c_std=c11',
    'buildtype=debugoptimized',
    'warning_level=2',
    'werror=true',
    'b_staticpic=false',
  ])

# The Otium C runtime from c/, built for 64-bit WebAssembly with Emscripten.
# The VM only: no native code, FFI or threads. Configure with
# --cross-file=cross/emscripten.txt; see Justfile.
fs = import('fs')
root = fs.parent(meson.project_source_root())
runtime_dir = '../c'
runtime_names = []
foreach entry : fs.read(runtime_dir / 'sources.list').strip().split('\n')
  if not entry.startswith('#')
    runtime_names += entry
  endif
endforeach
# runtime.c and main.c hold the paths to the library, which differ between
# the Node and browser builds, so they are compiled once for each.
common_sources = []
foreach name : runtime_names
  if name != 'runtime'
    common_sources += files(runtime_dir / 'src' / name + '.c')
  endif
endforeach
runtime_include = include_directories(runtime_dir / 'include', runtime_dir / 'src', '../native')
common = static_library('otium-common', common_sources,
  include_directories: runtime_include, implicit_include_directories: false)
helpers = static_library('otium-helpers', '../native/utf8/utf8.c', '../native/dev/transport.c',
  '../native/line/line-edit.c', '../native/vendor/bestline/bestline.c',
  include_directories: include_directories('../native', '../native/vendor/bestline'),
  override_options: ['warning_level=0', 'werror=false'])
entry_sources = files(runtime_dir / 'src/runtime.c', runtime_dir / 'src/main.c')

link_base = ['-sALLOW_MEMORY_GROWTH=1', '-sSTACK_SIZE=8MB', '-sEXIT_RUNTIME=1']

# For Node, with the host's files: the test harness runs this through the
# otium launcher beside it, with the library read from the source tree.
paths = {
  'OTIUM_LIBRARY_DIR': root / 'lib',
  'OTIUM_BOOTSTRAP_FILE': root / 'c/src/primitives.ot',
  'OTIUM_DEV_FILE': root / 'c/src/dev.ot',
}
node_args = []
foreach name, path : paths
  node_args += '-D@0@="@1@"'.format(name, fs.as_posix(path))
endforeach
executable('otium', entry_sources,
  c_args: node_args, include_directories: runtime_include, implicit_include_directories: false,
  link_with: [common, helpers], link_args: link_base + ['-sNODERAWFS=1'])
custom_target('launcher', input: 'otium.in', output: 'otium',
  command: ['install', '-m', '755', '@INPUT@', '@OUTPUT@'], build_by_default: true)

# For the browser, run in a worker: the library is embedded under /otium.
library_files = run_command('find', root / 'lib', '-type', 'f', check: true).stdout().strip().split('\n')
web_args = ['-DOTIUM_LIBRARY_DIR="/otium/lib"', '-DOTIUM_BOOTSTRAP_FILE="/otium/primitives.ot"',
  '-DOTIUM_DEV_FILE="/otium/dev.ot"']
executable('otium-web', entry_sources,
  c_args: web_args, include_directories: runtime_include, implicit_include_directories: false,
  link_with: [common, helpers],
  link_depends: library_files + files('../c/src/primitives.ot', '../c/src/dev.ot'),
  link_args: link_base + [
    '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=worker',
    '-sEXPORTED_RUNTIME_METHODS=FS',
    '--embed-file', root / 'lib@/otium/lib',
    '--embed-file', root / 'c/src/primitives.ot@/otium/primitives.ot',
    '--embed-file', root / 'c/src/dev.ot@/otium/dev.ot'])
foreach page : ['index.html', 'worker.js']
  configure_file(input: page, output: page, copy: true)
endforeach
