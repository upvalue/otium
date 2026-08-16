project('otium', 'c', 'cpp',
  version : '0.0.1',
  default_options : ['c_std=c99', 'cpp_std=c++20', 'warning_level=2', 'werror=true'])

add_project_arguments('-Wshadow', language : 'cpp')

# Runtime library: no exceptions, no RTTI (only the lib — doctest needs
# exceptions on in the test binary).
lib_args = ['-fno-exceptions', '-fno-rtti']
if get_option('gc_stress')
  lib_args += ['-DOT_GC_STRESS']
endif
if get_option('use_vm')
  lib_args += ['-DOT_USE_VM']
endif

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
)

libotium = static_library('otium',
  lib_sources, expander_h, prelude_h,
  include_directories : lib_inc,
  cpp_args : lib_args)

otium = executable('otium',
  'repl/main.cpp',
  include_directories : [lib_inc, bestline_inc],
  link_with : [libotium, bestline])

# Test binary: doctest (vendored single header at tests/doctest.h),
# exceptions ON (meson default), gc_stress flag still applies to the
# lib objects it links.
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
  cpp_args : get_option('gc_stress') ? ['-DOT_GC_STRESS'] : [],
  link_with : libotium)

test_timeout = get_option('gc_stress') ? 300 : 30
test('otium-tests', otium_tests, timeout : test_timeout)

cli_tests = find_program('tests/test_cli.py')
test('otium-cli', cli_tests,
  args : [otium, join_paths(meson.current_source_dir(), 'tests', 'cli-load.scm')],
  timeout : test_timeout)
