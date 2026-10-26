project('otium', ['c', 'cpp'],
  version: '0.1.0',
  meson_version: '>= 1.6',
  default_options: [
    'c_std=c23',
    'cpp_std=c++20',
    'buildtype=debugoptimized',
    'warning_level=3',
    'werror=true',
  ])

cc = meson.get_compiler('c')
fs = import('fs')
source_root = meson.project_source_root()
build_root = meson.project_build_root()
extension = host_machine.system() == 'darwin' ? 'dylib' : 'so'
dl = dependency('dl', required: false)
libm = cc.find_library('m', required: false)
native_include = include_directories('native')

# Native bridges shared by both runtimes. These are compiled with fewer
# warnings than the runtime because they follow their libraries' conventions.
native_options = ['warning_level=2']

utf8 = static_library('otium-utf8', 'native/utf8/utf8.c',
  pic: true, override_options: native_options)

bestline = static_library('bestline', 'native/vendor/bestline/bestline.c',
  pic: true, override_options: ['warning_level=0', 'werror=false'])

line = both_libraries('otium-line', 'native/line/line-edit.c',
  include_directories: include_directories('native/vendor/bestline'),
  link_whole: bestline, override_options: native_options)

dev = both_libraries('otium-dev', 'native/dev/transport.c',
  link_whole: utf8, override_options: native_options)

ffi_native = both_libraries('otium-ffi', 'native/ffi/native.c',
  link_whole: utf8, dependencies: dl, override_options: native_options)
# Two copies of the FFI test fixture, told apart by fixture_id().
ffi_fixtures = []
foreach identity : ['17', '29']
  ffi_fixtures += shared_module('ffi-fixture-' + identity, 'tests/ffi/fixture.c',
    c_args: '-DFIXTURE_ID=' + identity, dependencies: dependency('threads'),
    name_prefix: '', name_suffix: extension,
    build_by_default: false, override_options: native_options + ['c_std=c11'])
endforeach

libffi = dependency('libffi', required: get_option('ffi'))

utf8_checks = executable('otium-utf8-checks', 'tests/native/utf8.c',
  include_directories: include_directories('native/utf8'),
  link_with: utf8, override_options: native_options)

# The C runtime.
runtime_args = [
  '-Wshadow',
  '-DOT_HEAP_INIT=@0@'.format(get_option('heap_init')),
  '-DOT_HEAP_MAX=@0@'.format(get_option('heap_max')),
  '-DOT_MAX_DEPTH=@0@'.format(get_option('max_depth')),
  '-DOT_GC_NURSERY_BYTES=@0@'.format(get_option('gc_nursery_bytes')),
  '-DOT_GC_OLD_CHUNK_BYTES=@0@'.format(get_option('gc_old_chunk_bytes')),
  '-DOT_GC_LARGE_OBJECT_BYTES=@0@'.format(get_option('gc_large_object_bytes')),
  '-DOT_GC_MARK_STACK_ENTRIES=@0@'.format(get_option('gc_mark_stack_entries')),
  '-DOT_GC_TIMING=@0@'.format(get_option('gc_timing')),
  get_option('gc') == 'gen' ? '-DOT_GC_GEN' : '-DOT_GC_SEMI',
]
runtime_sources = files(
  'c/src/otium.c', 'c/src/ot-gc-common.c', 'c/src/ot-gc-@0@.c'.format(get_option('gc')),
  'c/src/ot-posix.c', 'c/src/ot-ffi.c', 'c/src/ot-cli.c')
runtime_deps = [libm]
runtime_links = []
if libffi.found()
  runtime_args += '-DOT_WITH_FFI'
  runtime_deps += [libffi, dl]
  runtime_links += ffi_native.get_static_lib()
endif

libotium = static_library('otium', runtime_sources,
  c_args: runtime_args, dependencies: runtime_deps, link_with: runtime_links)

otium = executable('otium', 'c/src/main.c', 'c/src/ot-ext-demo.c',
  c_args: runtime_args + ['-DOT_LIB_DIR="@0@"'.format(source_root / 'lib')],
  include_directories: native_include,
  link_with: [libotium, line.get_static_lib(), dev.get_static_lib()])

test_runtime = executable('test-runtime', 'c/tests/test_runtime.c',
  c_args: runtime_args, include_directories: include_directories('c/src'),
  link_with: libotium)

gc_bench = executable('gc-bench', 'c/bench/gc_bench.c',
  c_args: runtime_args, include_directories: include_directories('c/src'),
  link_with: libotium, build_by_default: false)

# The optional UI bridge: SDL_GPU rendering, SDL_ttf text and Yoga layout.
sdl3 = dependency('sdl3', version: '>= 3.4.12', required: get_option('ui'))
sdl3_ttf = dependency('sdl3-ttf', version: '>= 3.2.2', required: get_option('ui'))
glslc = find_program('glslc', required: get_option('ui'))
# Yoga is always the pinned subproject, never a system copy.
yoga_project = subproject('yoga',
  required: get_option('ui').disable_auto_if(not (sdl3.found() and sdl3_ttf.found() and glslc.found())))
ui = sdl3.found() and sdl3_ttf.found() and glslc.found() and yoga_project.found()
if ui
  yoga = yoga_project.get_variable('yoga_dep')
  subproject('plutovg', default_options: [
    'default_library=static', 'examples=disabled', 'tests=disabled',
    'font-face-cache-load=disabled', 'warning_level=1', 'werror=false'])
  svg_project = subproject('plutosvg', default_options: [
    'default_library=static', 'freetype=disabled', 'examples=disabled',
    'tests=disabled', 'warning_level=1', 'werror=false'])
  svg = svg_project.get_variable('plutosvg_dep')
  shaders = []
  foreach stage : ['vert', 'frag']
    shaders += custom_target('rect.@0@.spv'.format(stage),
      input: 'native/ui/shaders/rect.@0@'.format(stage),
      output: 'rect.@0@.spv'.format(stage),
      command: [glslc, '@INPUT@', '-o', '@OUTPUT@'])
  endforeach
  ui_args = ['-DOTUI_FONT_DIR="@0@"'.format(source_root / 'native/ui/fonts')]
  ui_library = shared_library('otium-ui',
    'native/ui/renderer.c', 'native/ui/text.c', 'native/ui/layout.cpp', shaders,
    c_args: ui_args + ['--embed-dir=' + build_root], cpp_args: ui_args,
    dependencies: [sdl3, sdl3_ttf, yoga, svg, libm], override_options: native_options)
  ui_events = shared_library('otium-ui-test-events', 'tests/ui/events.c',
    c_args: ui_args,
    dependencies: sdl3, override_options: native_options)
  ui_text_reference = executable('otium-ui-text-reference', 'tests/ui/text-reference.c',
    c_args: ui_args,
    dependencies: [sdl3, sdl3_ttf], override_options: native_options)
  ui_caret_checks = executable('otium-ui-caret-checks', 'tests/ui/caret-checks.c',
    c_args: ui_args,
    link_with: ui_library, dependencies: [sdl3, sdl3_ttf, libm], override_options: native_options)
  sdl_fixture = shared_module('sdl-fixture', 'tests/sdl/fixture.c',
    dependencies: sdl3, name_prefix: '', name_suffix: extension,
    build_by_default: false, override_options: native_options + ['c_std=c11'])

  # Native installation paths, read by Otium as data.
  sdl_library = sdl3.get_variable(pkgconfig: 'libdir') / 'libSDL3.' + extension
  configure_file(output: 'ui-config.ot', configuration: {
    'library': build_root / 'libotium-ui.' + extension, 'sdl': sdl_library,
    'icons': source_root / 'native/ui/icons'},
    input: 'native/ui/ui-config.ot.in')
  configure_file(output: 'ui-test-config.ot', configuration: {
    'library': build_root / 'libotium-ui-test-events.' + extension},
    input: 'native/ui/ui-test-config.ot.in')
endif

# manse, the development environment built on Otium.
subdir('manse/native')

# Tests. tests/run lists the tests defined in tests.py files, and each runs as
# tests/run NAME from the source root; the launchers find this build through
# OTIUM_BUILD. GC stress is opt-in: meson test --setup stress.
python = find_program('python3')
chez = find_program('chez', 'scheme', required: false)
harness = files('tests/run')
test_env = environment({
  'OTIUM_BUILD': build_root,
  'PYTHONPATH': source_root / 'tools' + ':' + source_root,
})
if chez.found()
  test_env.set('CHEZ', chez.full_path())
endif
add_test_setup('default', is_default: true, env: test_env,
  exclude_suites: ['native', 'gpu', 'bench'])
stress_env = environment({
  'OTIUM_BUILD': build_root,
  'PYTHONPATH': source_root / 'tools' + ':' + source_root,
  'OTIUM_GC_STRESS': '1',
})
if chez.found()
  stress_env.set('CHEZ', chez.full_path())
endif
add_test_setup('stress', env: stress_env,
  exclude_suites: ['native', 'gpu', 'bench'], timeout_multiplier: 4)
add_test_setup('all', env: test_env)
test_kwargs = {'workdir': source_root, 'env': test_env}

# Compiled checks.
test('runtime', test_runtime, suite: 'c', kwargs: test_kwargs)
test('utf8', utf8_checks, suite: 'native', kwargs: test_kwargs)
if ui
  test('ui caret', ui_caret_checks, suite: 'native', kwargs: test_kwargs)
endif

features = ['ffi']
needed = {'': [otium], 'ffi': [ffi_native] + ffi_fixtures}
if chez.found()
  features += 'chez'
endif
if libffi.found()
  features += 'c-ffi'
endif
if ui
  # Display tests share the window server and build/ui-test-artifacts/, so
  # they're registered as not parallel.
  features += 'ui'
  needed += {'ui': [ffi_native, ui_library, ui_events, ui_text_reference, sdl_fixture]}
endif
listing = run_command(python, harness, '--list', ','.join(features), check: true,
  env: test_env).stdout().strip().split('\n')
foreach line : listing
  fields = line.split('\t')
  if fields[0] == 'file'
    # Reconfigure when a definition file changes.
    fs.read(fields[1])
    continue
  endif
  depends = needed['']
  foreach need : fields[5].split(',')
    if need in needed
      depends += needed[need]
    endif
  endforeach
  test(fields[1], python, args: [harness, fields[1]], suite: fields[2],
    timeout: fields[3].to_int(), is_parallel: fields[4] == '1', depends: depends,
    kwargs: test_kwargs)
endforeach
