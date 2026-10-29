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

# Native bridges, compiled with fewer warnings than the runtime because they
# follow their libraries' conventions.
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

utf8_checks = executable('otium-utf8-checks', 'tests/native/utf8.c',
  include_directories: include_directories('native/utf8'),
  link_with: utf8, override_options: native_options)

python = find_program('python3')
subdir('c')
# Built here so the launcher finds it in OTIUM_BUILD.
otium = executable('otium', 'c/src/main.c',
  c_args: runtime_args, include_directories: native_include, link_with: line.get_static_lib(),
  dependencies: libotium_dep, override_options: runtime_options)

# The boot file native processes start from: the VM compiles the expander,
# core and the native compiler to native code.
if native_enabled
  boot_sources = files('lib/otium/expander.ot', 'lib/otium/core.ot', 'lib/otium/loop.ot',
    'c/src/primitives.ot', 'lib/otium/compiler.ot', 'lib/otium/compiler/convert.ot',
    'lib/otium/compiler/lower.ot', 'lib/otium/compiler/alloc.ot', 'lib/otium/compiler/emit.ot',
    'lib/otium/compiler/fasl.ot', 'lib/otium/compiler/boot.ot', 'lib/otium/compiler/known.ot',
    'lib/otium/compiler/options.ot', 'lib/otium/compiler/rescope.ot', 'lib/otium/compiler/ir.ot',
    'lib/otium/compiler/types.ot')
  boot_command = [otium, '--c.vm', '--no-project', '-L', source_root / 'lib', '-e',
    '(require otium.compiler.boot :as boot) (boot/build-standard "@OUTPUT@" "@0@" "@1@") (exit 0)'.format(
      source_root / 'lib', source_root / 'c/src/primitives.ot')]
  custom_target('otium.boot', output: 'otium.boot', depend_files: boot_sources,
    command: boot_command, build_by_default: true)
  # The same, compiled with the optimizer switched off, for comparing
  # (benchmarks/opt-compare.py): OTIUM_BOOT picks it.
  custom_target('otium-noopt.boot', output: 'otium-noopt.boot', depend_files: boot_sources,
    command: boot_command, env: {'OTIUM_OPT': 'none'}, build_by_default: true)
endif

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
# tests/run NAME from the source root; the launcher finds this build through
# OTIUM_BUILD. GC stress is opt-in: meson test --setup stress.
harness = files('tests/run')
test_variables = {
  'OTIUM_BUILD': build_root,
  'PYTHONPATH': source_root / 'tools' + ':' + source_root,
}
test_env = environment(test_variables)
stress_env = environment(test_variables + {'OTIUM_GC_STRESS': '1'})
vm_env = environment(test_variables + {'OTIUM_VM': '1'})
add_test_setup('default', is_default: true, env: test_env,
  exclude_suites: ['native', 'gpu', 'bench'])
add_test_setup('stress', env: stress_env,
  exclude_suites: ['native', 'gpu', 'bench'], timeout_multiplier: 4)
add_test_setup('all', env: test_env)
add_test_setup('vm', env: vm_env, exclude_suites: ['native', 'gpu', 'bench'])
test_kwargs = {'workdir': source_root, 'env': test_env}

# Compiled checks.
test('unit', unit_tests, suite: 'c', timeout: 300,
  depends: ffi_fixtures + ffi_extra, kwargs: test_kwargs)
test('utf8', utf8_checks, suite: 'native', kwargs: test_kwargs)
if ui
  test('ui caret', ui_caret_checks, suite: 'native', kwargs: test_kwargs)
endif

features = ['ffi', 'c-host']
needed = {'': [otium], 'ffi': [ffi_native] + ffi_fixtures,
  'c-host': [foreign_control, ffi_extra, extension_demo, otium_amalgamation] + ffi_fixtures}
if ui
  # Display tests share the window server and the build's ui-test-artifacts/, so
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
