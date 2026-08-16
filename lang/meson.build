project('otium', 'c',
  version : '0.0.1',
  # Meson's own default buildtype is 'debug' (-O0), which made every benchmark
  # figure recorded before this a measurement of unoptimized code. Default to
  # -O2 with symbols; override with -Dbuildtype=debug when stepping through.
  # c2x = C23; the runtime requires GCC >= 13 or Clang >= 17.
  default_options : ['c_std=c2x', 'warning_level=2', 'werror=true',
                     'buildtype=debugoptimized'])

add_project_arguments('-Wshadow', language : 'c')

lib_args = []
cc = meson.get_compiler('c')
has_computed_goto = cc.compiles('''
  int main() {
    static void* labels[] = {&&done};
    goto *labels[0];
  done:
    return 0;
  }
''', name : 'computed goto')
if get_option('computed_goto').enabled() and not has_computed_goto
  error('computed goto was requested but the C compiler does not support it')
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
  'src/vec.c',
  'src/numio.c',
  'src/heap.c',
  'src/foreign.c',
  'src/intern.c',
  'src/state.c',
  'src/code.c',
  'src/vm.c',
  'src/compile.c',
  'src/ns.c',
  'src/eval.c',
  'src/reader.c',
  'src/printer.c',
  'src/builtins/arith.c',
  'src/builtins/cond.c',
  'src/builtins/data.c',
  'src/builtins/expand.c',
  'src/builtins/string.c',
  'src/builtins/sys.c',
  'src/builtins/time.c',
)

libotium = static_library('otium',
  lib_sources, expander_h, prelude_h,
  include_directories : lib_inc,
  c_args : lib_args)

otium_sources = files('repl/main.c')
otium_c_args = []
otium_links = [libotium, bestline]
otium_dependencies = []

if get_option('ext_demo')
  otium_sources += files('ext/demo/demo_ext.c')
  otium_c_args += ['-DOT_EXT_DEMO']
endif

if get_option('ext_ray')
  raylib_dep = dependency('raylib', required : true)
  otium_sources += files('ext/ray/ray_ext.c')
  otium_c_args += ['-DOT_EXT_RAY']
  otium_dependencies += [raylib_dep]
endif

otium = executable('otium',
  otium_sources,
  include_directories : [lib_inc, bestline_inc],
  c_args : otium_c_args,
  dependencies : otium_dependencies,
  link_with : otium_links)

# Test binary: minimal vendored-in-place harness at tests/ctest.h.
test_sources = files(
  'tests/main.c',
  'tests/test_substrate.c',
  'tests/test_reader.c',
  'tests/test_eval.c',
  'tests/test_builtins.c',
  'tests/test_vm.c',
  'tests/test_compile.c',
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

if get_option('ext_demo')
  test('otium-demo-extension', otium,
    args : [
      '--path', join_paths(meson.current_source_dir(), 'ext', 'demo'),
      join_paths(meson.current_source_dir(), 'tests', 'ext-demo.scm'),
    ],
    timeout : test_timeout)
endif

if get_option('ext_ray')
  test('otium-ray-extension', otium,
    args : [
      '--path', join_paths(meson.current_source_dir(), 'ext', 'ray'),
      join_paths(meson.current_source_dir(), 'tests', 'ext-ray.scm'),
    ],
    timeout : test_timeout)
endif
