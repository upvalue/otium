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

libffi = dependency('libffi', required: get_option('ffi'))
if libffi.found()
  ffi_native = both_libraries('otium-ffi', 'native/ffi/native.c',
    link_whole: utf8, dependencies: dl, override_options: native_options)
endif

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
    dependencies: [sdl3, sdl3_ttf, yoga, libm], override_options: native_options)
  ui_events = shared_library('otium-ui-test-events', 'tests/ui/events.c',
    c_args: ui_args,
    dependencies: sdl3, override_options: native_options)
  ui_text_reference = executable('otium-ui-text-reference', 'tests/ui/text-reference.c',
    c_args: ui_args,
    dependencies: [sdl3, sdl3_ttf], override_options: native_options)
  ui_caret_checks = executable('otium-ui-caret-checks', 'tests/ui/caret-checks.c',
    c_args: ui_args,
    link_with: ui_library, dependencies: [sdl3, sdl3_ttf, libm], override_options: native_options)

  # Native installation paths, read by Otium as data.
  sdl_library = sdl3.get_variable(pkgconfig: 'libdir') / 'libSDL3.' + extension
  configure_file(output: 'ui-config.ot', configuration: {
    'library': build_root / 'libotium-ui.' + extension, 'sdl': sdl_library},
    input: 'native/ui/ui-config.ot.in')
  configure_file(output: 'ui-test-config.ot', configuration: {
    'library': build_root / 'libotium-ui-test-events.' + extension},
    input: 'native/ui/ui-test-config.ot.in')
endif

# Tests. Run from the source root; the launchers find this build through
# OTIUM_BUILD. GC stress is opt-in: meson test --setup stress.
python = find_program('python3')
chez = find_program('chez', 'scheme', required: false)
sh = find_program('sh')
run = files('tests/run')
launcher = files('otium')
test_env = environment({
  'OTIUM_BUILD': build_root,
  'PYTHONPATH': source_root / 'tools' + ':' + source_root,
})
add_test_setup('default', is_default: true, env: test_env,
  exclude_suites: ['native', 'gpu', 'bench'])
stress_env = environment({
  'OTIUM_BUILD': build_root,
  'PYTHONPATH': source_root / 'tools' + ':' + source_root,
  'OTIUM_GC_STRESS': '1',
})
add_test_setup('stress', env: stress_env,
  exclude_suites: ['native', 'gpu', 'bench'], timeout_multiplier: 4)
add_test_setup('all', env: test_env)

runtimes = chez.found() ? ['c', 'chez'] : ['c']
test_kwargs = {'workdir': source_root, 'env': test_env}

# C runtime.
test('runtime', test_runtime, suite: 'c', kwargs: test_kwargs)
test('hygiene', python, args: [source_root / 'c/tests/check_hygiene.py', source_root / 'c'],
  suite: 'c', kwargs: test_kwargs)
foreach name : ['01-reader', '02-types', '03-equality', '04-tables', '05-control',
                '06-closures', '07-macros', '08-conditions', '09-params', '11-multimethods']
  test('conformance ' + name, run,
    args: ['--expect', source_root / 'c/tests/otium/expected' / name + '.txt', '--',
           otium, source_root / 'c/tests/otium' / name + '.ot'],
    suite: 'c', kwargs: test_kwargs)
endforeach
test('multimethods', python, args: [source_root / 'c/tests/test_multimethods.py', otium],
  suite: 'c', timeout: 180, kwargs: test_kwargs)
test('cli c', python, args: [source_root / 'c/tests/test_cli.py', otium,
  source_root / 'c/tests/cli-load.ot', source_root / 'c/tests/cli-load-second.ot'],
  suite: 'c', kwargs: test_kwargs)
test('extension', otium, args: ['--no-project', '--path', source_root / 'c/tests/modules',
  source_root / 'c/tests/ext-demo.ot'], suite: 'c', kwargs: test_kwargs)

# Chez runtime.
if chez.found()
  foreach name : ['tests', 'macro-tests', 'unary-call-tests', 'generic-tests']
    test('chez ' + name, run, args: ['--scratch', '--', chez, '--libdirs', source_root / 'chez',
      '--script', source_root / 'chez/tests' / name + '.ss'], suite: 'chez', kwargs: test_kwargs)
  endforeach
  foreach name : ['append-tests', 'condition-tests', 'environment-tests', 'reader-tests']
    test('chez ' + name, chez, args: ['--libdirs', source_root / 'chez',
      '--script', source_root / 'chez/tests' / name + '.ss'], suite: 'chez', kwargs: test_kwargs)
  endforeach
  test('chez help', files('chez/run'), args: ['--help'], suite: 'chez', kwargs: test_kwargs)
  test('format', files('chez/run'), args: ['--no-project', 'tests/format/checks.ot'],
    suite: 'shared', kwargs: test_kwargs)
  test('format corpus', files('chez/run'),
    args: ['--no-project', 'tests/format/corpus.ot', '--', 'lib', 'examples', 'benchmarks'],
    suite: 'shared', timeout: 120, kwargs: test_kwargs)
endif

# Otium checks that run unchanged on both runtimes. Each exits nonzero on failure.
checks = {
  'expander': ['tests/expander/checks.ot'],
  'classes': ['tests/classes/checks.ot'],
  'match': ['tests/match/checks.ot'],
  'ui': ['tests/ui/checks.ot'],
  'ui calculator': ['-L', 'examples', 'tests/ui/calculator-checks.ot'],
  'ui editing': ['-L', 'examples', 'tests/ui/editing-checks.ot'],
  'ui policy': ['tests/ui/policy-checks.ot'],
}
foreach runtime : runtimes
  foreach name, arguments : checks
    test(name + ' ' + runtime, launcher, args: ['--runtime=' + runtime, '--no-project'] + arguments,
      suite: 'shared', kwargs: test_kwargs)
  endforeach
endforeach

# Drivers that control processes, sockets or temporary module trees.
foreach name : ['conditions', 'reader', 'library', 'modules', 'cli', 'dev', 'help']
  test(name, python, args: [source_root / 'tests/test_@0@.py'.format(name)],
    suite: 'shared', timeout: 120, kwargs: test_kwargs)
endforeach
test('generated', python, args: [source_root / 'tools/generate-cli.py', '--check'],
  suite: 'shared', kwargs: test_kwargs)
test('launcher', python, args: ['-m', 'unittest', 'discover', '-s', source_root / 'tools/tests'],
  suite: 'shared', kwargs: test_kwargs)
test('docs', files('tools/check-docs'), suite: 'shared', kwargs: test_kwargs)

# Native components: FFI, SDL and the UI bridge.
test('utf8', utf8_checks, suite: 'native', kwargs: test_kwargs)
if libffi.found()
  test('ffi', python, args: [source_root / 'tests/test_ffi.py'], suite: 'native',
    depends: ffi_native, kwargs: test_kwargs)
  foreach runtime : runtimes
    test('ui application ' + runtime, launcher,
      args: ['--runtime=' + runtime, '--no-project', 'tests/ui/application.ot'],
      suite: 'native', depends: ffi_native, kwargs: test_kwargs)
  endforeach
endif
if libffi.found() and ui
  # Display tests share the window server and build/ui-test-artifacts/, so they
  # run one at a time.
  ui_depends = [ffi_native, ui_library, ui_events, ui_text_reference]
  test('ui caret', ui_caret_checks, suite: 'native', kwargs: test_kwargs)
  foreach runtime : runtimes
    test('ui layout ' + runtime, launcher,
      args: ['--runtime=' + runtime, '--no-project', '-L', 'examples', 'tests/ui/layout.ot'],
      suite: 'native', depends: ui_depends, timeout: 90, kwargs: test_kwargs)
  endforeach
  test('sdl', python, args: [source_root / 'tests/test_sdl.py'], suite: 'native',
    depends: ui_depends, timeout: 300, is_parallel: false, kwargs: test_kwargs)
  test('ui gpu', python, args: [source_root / 'tests/test_ui_gpu.py'], suite: 'native',
    depends: ui_depends, timeout: 300, is_parallel: false, kwargs: test_kwargs)
  test('dev ui', python, args: [source_root / 'tests/test_dev_ui.py'], suite: 'gpu',
    depends: ui_depends, timeout: 300, is_parallel: false, kwargs: test_kwargs)
  test('sdl accelerated', python, args: [source_root / 'tests/test_sdl.py', '--require-accelerated'],
    suite: 'gpu', depends: ui_depends, timeout: 300, is_parallel: false, kwargs: test_kwargs)
  test('ui gpu required', python, args: [source_root / 'tests/test_ui_gpu.py', '--require-gpu'],
    suite: 'gpu', depends: ui_depends, timeout: 300, is_parallel: false, kwargs: test_kwargs)
endif

# Reduced-input correctness checks for the R7RS benchmark ports.
foreach runtime : runtimes
  test('r7rs ports ' + runtime, python,
    args: [source_root / 'benchmarks/r7rs/test_ports.py', '--runtime', runtime],
    suite: 'bench', timeout: 300, kwargs: test_kwargs)
endforeach
