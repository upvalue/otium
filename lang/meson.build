project('otium', 'c', 'cpp',
  version : '0.0.1',
  # Meson's own default buildtype is 'debug' (-O0), which made every benchmark
  # figure recorded before this a measurement of unoptimized code. Default to
  # -O2 with symbols; override with -Dbuildtype=debug when stepping through.
  default_options : ['c_std=c99', 'cpp_std=c++20', 'warning_level=2', 'werror=true',
                     'buildtype=debugoptimized'])

add_project_arguments('-Wshadow', language : 'cpp')

# Runtime library: no exceptions, no RTTI (only the lib — doctest needs
# exceptions on in the test binary).
lib_args = ['-fno-exceptions', '-fno-rtti']
cpp = meson.get_compiler('cpp')
has_computed_goto = cpp.compiles('''
  int main() {
    static void* labels[] = {&&done};
    goto *labels[0];
  done:
    return 0;
  }
''', name : 'computed goto')
if get_option('computed_goto').enabled() and not has_computed_goto
  error('computed goto was requested but the C++ compiler does not support it')
endif
if not get_option('computed_goto').disabled() and has_computed_goto
  lib_args += ['-DOT_COMPUTED_GOTO']
endif

lib_inc = include_directories('src')
bestline_inc = include_directories('vendor/bestline')
bestline = static_library('bestline',
  'vendor/bestline/bestline.c',
  include_directories : bestline_inc,
  override_options : ['warning_level=0', 'werror=false'])

# The embedded expander and prelude are required runtime components. Generate
# them as C arrays so every supported VM starts with the same core bindings.
python = find_program('python3')
embed_py = files('tools/embed.py')
expander_h = custom_target('ot_expander_scm',
  input : 'prelude/expander.scm',
  output : 'ot_expander_scm.h',
  command : [python, embed_py, '@INPUT@', 'ot_expander_scm', '@OUTPUT@'])
prelude_h = custom_target('ot_prelude_scm',
  input : 'prelude/prelude.scm',
  output : 'ot_prelude_scm.h',
  command : [python, embed_py, '@INPUT@', 'ot_prelude_scm', '@OUTPUT@'])

lib_sources = files(
  'src/heap.cpp',
  'src/foreign.cpp',
  'src/intern.cpp',
  'src/state.cpp',
  'src/code.cpp',
  'src/vm.cpp',
  'src/compile.cpp',
  'src/ns.cpp',
  'src/eval.cpp',
  'src/reader.cpp',
  'src/printer.cpp',
  'src/builtins/arith.cpp',
  'src/builtins/cond.cpp',
  'src/builtins/data.cpp',
  'src/builtins/expand.cpp',
  'src/builtins/string.cpp',
  'src/builtins/sys.cpp',
  'src/builtins/time.cpp',
)

libotium = static_library('otium',
  lib_sources, expander_h, prelude_h,
  include_directories : lib_inc,
  cpp_args : lib_args)

otium_sources = files('repl/main.cpp')
otium_cpp_args = []
otium_links = [libotium, bestline]
otium_dependencies = []

if get_option('ext_demo')
  otium_sources += files('ext/demo/demo_ext.cpp')
  otium_cpp_args += ['-DOT_EXT_DEMO']
endif

if get_option('ext_raylib')
  raylib_dep = dependency('raylib', required : true)
  otium_sources += files('ext/raylib/raylib_ext.cpp')
  otium_cpp_args += ['-DOT_EXT_RAYLIB']
  otium_dependencies += [raylib_dep]
endif

otium = executable('otium',
  otium_sources,
  include_directories : [lib_inc, bestline_inc],
  cpp_args : otium_cpp_args,
  dependencies : otium_dependencies,
  link_with : otium_links)

# Test binary: doctest (vendored single header at tests/doctest.h),
# exceptions ON (meson default).
test_sources = files(
  'tests/main.cpp',
  'tests/test_substrate.cpp',
  'tests/test_reader.cpp',
  'tests/test_eval.cpp',
  'tests/test_builtins.cpp',
  'tests/test_vm.cpp',
  'tests/test_compile.cpp',
)
otium_tests = executable('otium-tests',
  test_sources,
  include_directories : [lib_inc, include_directories('tests')],
  link_with : libotium)

test_timeout = 30
test('otium-tests', otium_tests, timeout : test_timeout)

hygiene_check = files('tests/check_hygiene.py')
test('source-hygiene', python,
  args : [hygiene_check, meson.current_source_dir()],
  timeout : test_timeout)

cli_tests = find_program('tests/test_cli.py')
test('otium-cli', cli_tests,
  args : [otium, join_paths(meson.current_source_dir(), 'tests', 'cli-load.scm')],
  timeout : test_timeout)

r7rs_port_tests = find_program('benchmarks/r7rs/test_ports.py')
test('r7rs-ports', r7rs_port_tests,
  args : [otium],
  timeout : test_timeout)

# --- C port (parallel tree under c/) ----------------------------------------
# Ported from the C++ runtime; validated against the same language-level test
# corpus. Requires a C23-capable compiler (GCC >= 13 / Clang >= 17).
cc = meson.get_compiler('c')
c_std_opt = 'c_std=c2x'
c_has_computed_goto = cc.compiles('''
  int main() {
    static void* labels[] = {&&done};
    goto *labels[0];
  done:
    return 0;
  }
''', name : 'computed goto (c)')
c_lib_args = ['-Wshadow']
if not get_option('computed_goto').disabled() and c_has_computed_goto
  c_lib_args += ['-DOT_COMPUTED_GOTO']
endif

c_lib_inc = include_directories('c/src')
c_lib_sources = files(
  'c/src/vec.c',
  'c/src/numio.c',
  'c/src/heap.c',
  'c/src/foreign.c',
  'c/src/intern.c',
  'c/src/state.c',
  'c/src/code.c',
  'c/src/vm.c',
  'c/src/compile.c',
  'c/src/ns.c',
  'c/src/eval.c',
  'c/src/reader.c',
  'c/src/printer.c',
  'c/src/builtins/arith.c',
  'c/src/builtins/cond.c',
  'c/src/builtins/data.c',
  'c/src/builtins/expand.c',
  'c/src/builtins/string.c',
  'c/src/builtins/sys.c',
  'c/src/builtins/time.c',
)

libotium_c = static_library('otium_c',
  c_lib_sources, expander_h, prelude_h,
  include_directories : c_lib_inc,
  c_args : c_lib_args,
  override_options : [c_std_opt])

otium_c_sources = files('c/repl/main.c')
otium_c_args = []
otium_c_dependencies = []
if get_option('ext_demo')
  otium_c_sources += files('c/ext/demo/demo_ext.c')
  otium_c_args += ['-DOT_EXT_DEMO']
endif
if get_option('ext_raylib')
  otium_c_sources += files('c/ext/raylib/raylib_ext.c')
  otium_c_args += ['-DOT_EXT_RAYLIB']
  otium_c_dependencies += [dependency('raylib', required : true)]
endif

otium_c = executable('otium-c',
  otium_c_sources,
  include_directories : [c_lib_inc, bestline_inc],
  c_args : otium_c_args,
  dependencies : otium_c_dependencies,
  link_with : [libotium_c, bestline],
  override_options : [c_std_opt])

c_test_sources = files(
  'c/tests/main.c',
  'c/tests/test_substrate.c',
  'c/tests/test_reader.c',
  'c/tests/test_eval.c',
  'c/tests/test_builtins.c',
  'c/tests/test_vm.c',
  'c/tests/test_compile.c',
)
otium_c_tests = executable('otium-c-tests',
  c_test_sources,
  include_directories : [c_lib_inc, include_directories('c/tests')],
  link_with : libotium_c,
  override_options : [c_std_opt])

test('otium-c-tests', otium_c_tests, timeout : test_timeout)
test('otium-c-cli', cli_tests,
  args : [otium_c, join_paths(meson.current_source_dir(), 'tests', 'cli-load.scm')],
  timeout : test_timeout)
test('r7rs-ports-c', r7rs_port_tests,
  args : [otium_c],
  timeout : test_timeout)

if get_option('ext_demo')
  test('otium-demo-extension-c', otium_c,
    args : [
      '--path', join_paths(meson.current_source_dir(), 'ext', 'demo'),
      join_paths(meson.current_source_dir(), 'tests', 'ext-demo.scm'),
    ],
    timeout : test_timeout)
  test('otium-demo-extension', otium,
    args : [
      '--path', join_paths(meson.current_source_dir(), 'ext', 'demo'),
      join_paths(meson.current_source_dir(), 'tests', 'ext-demo.scm'),
    ],
    timeout : test_timeout)
endif

if get_option('ext_raylib')
  test('otium-raylib-extension-c', otium_c,
    args : [
      '--path', join_paths(meson.current_source_dir(), 'ext', 'raylib'),
      join_paths(meson.current_source_dir(), 'tests', 'ext-raylib.scm'),
    ],
    timeout : test_timeout)
  test('otium-raylib-extension', otium,
    args : [
      '--path', join_paths(meson.current_source_dir(), 'ext', 'raylib'),
      join_paths(meson.current_source_dir(), 'tests', 'ext-raylib.scm'),
    ],
    timeout : test_timeout)
endif
