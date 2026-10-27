# The C runtime. See docs/runtime-rewrite.md.

runtime_sources = files(
  'src/heap.c', 'src/reader.c', 'src/tokens.c', 'src/runtime.c', 'src/compiler.c', 'src/vm.c',
  'src/builtins.c', 'src/numeric.c', 'src/time.c', 'src/filesystem.c', 'src/process.c',
  'src/ports.c', 'src/strings.c', 'src/bytevectors.c', 'src/reflection.c', 'src/support.c',
  'src/conditions.c', 'src/restarts.c', 'src/cleanup.c', 'src/params.c', 'src/classes.c',
  'src/instances.c', 'src/records.c', 'src/foreign.c', 'src/foreign_types.c',
  'src/collections.c', 'src/tables.c', 'src/reclaim.c', 'src/modules.c', 'src/natives.c')

# The runtime is C11 with GNU builtins and computed goto, so it skips the
# project's C23 and -Wpedantic.
runtime_headers = files('src/internal.h', 'src/heap.h', 'src/reader.h', 'src/foreign.h',
  'src/foreign_types.h')
runtime_options = ['c_std=c11', 'warning_level=2']
runtime_include = include_directories('include', 'src')
runtime_deps = [dependency('libffi'), dependency('threads'), dl, libm]
runtime_args = [
  '-DOTIUM_LIBRARY_DIR="@0@"'.format(source_root / 'lib'),
  '-DOTIUM_BOOTSTRAP_FILE="@0@"'.format(source_root / 'c/src/primitives.ot'),
  '-DOTIUM_TEST_FIXTURE_DIR="@0@"'.format(build_root),
]
checks = get_option('runtime_checks')
if checks != 'none'
  runtime_args += '-DOTIUM_CHECK_KINDS'
endif
if checks == 'roots'
  runtime_args += '-DOTIUM_CHECK_ROOTS'
endif
if get_option('vm_switch')
  runtime_args += '-DOTIUM_VM_SWITCH'
endif

libotium = static_library('otium', runtime_sources,
  c_args: runtime_args, include_directories: runtime_include, dependencies: runtime_deps,
  override_options: runtime_options)
libotium_dep = declare_dependency(link_with: libotium,
  include_directories: include_directories('include'), dependencies: runtime_deps)

# The single-file distribution: otium.h, otium.c and main.c.
amalgamation = custom_target('amalgamation',
  input: runtime_sources, output: ['otium.c', 'otium.h', 'main.c'],
  command: [python, files('tools/amalgamate.py'), '@OUTDIR@', '@INPUT@'],
  depend_files: files('include/otium.h', 'src/main.c') + runtime_headers)
otium_amalgamation = executable('otium-amalgamation', amalgamation[0], amalgamation[2],
  c_args: runtime_args, dependencies: runtime_deps, override_options: runtime_options,
  build_by_default: false)

# Each tests/unit/NAME.c is one suite, and all of them link into one runner.
# value.c is built a second time with the portable integer representation.
unit_suites = [
  'bytevectors', 'cleanup', 'conditions', 'control', 'defonce', 'evaluation', 'file-ports',
  'filesystem', 'foreign', 'foreign-reload', 'generics', 'handlers', 'heap', 'heap-growth',
  'instances', 'interrupt', 'io-host', 'macros', 'methods', 'modules', 'namespaces', 'numeric',
  'params', 'port-reader', 'port-shutdown', 'ports', 'process', 'properties', 'quasiquote',
  'reader', 'records', 'reflection', 'reload', 'restarts', 'runtime', 'scope-roots',
  'shared-expander', 'slot-reuse', 'strings', 'time', 'tokens', 'value']
unit_include = include_directories('tests/unit/support', 'src')
unit_support = static_library('otium-test-support',
  'tests/unit/support/test.c', 'tests/unit/support/helpers.c',
  c_args: runtime_args, include_directories: unit_include, dependencies: libotium_dep,
  override_options: runtime_options)
declarations = []
entries = []
unit_sources = []
foreach suite : unit_suites
  symbol = suite.replace('-', '_')
  declarations += 'extern const test_suite @0@_suite;'.format(symbol)
  entries += '    &@0@_suite,'.format(symbol)
  unit_sources += 'tests/unit/@0@.c'.format(suite)
endforeach
declarations += 'extern const test_suite value_portable_suite;'
entries += '    &value_portable_suite,'
unit_main = configure_file(input: 'tests/unit/main.c.in', output: 'unit-main.c',
  configuration: {'DECLARATIONS': '\n'.join(declarations), 'ENTRIES': '\n'.join(entries)})
value_portable = static_library('otium-test-value-portable', 'tests/unit/value.c',
  c_args: runtime_args + '-DOTIUM_PORTABLE_INTS', include_directories: unit_include,
  dependencies: libotium_dep, override_options: runtime_options)
unit_tests = executable('otium-unit-tests', unit_main, unit_sources,
  c_args: runtime_args, include_directories: unit_include, dependencies: libotium_dep,
  link_with: [unit_support, value_portable], override_options: runtime_options)

# A host program for the callback abort checks in tests.py.
foreign_control = executable('otium-foreign-control', 'tests/unit/hosts/foreign-control.c',
  c_args: runtime_args, include_directories: unit_include, dependencies: libotium_dep,
  link_with: unit_support, override_options: runtime_options)

# A host with native procedures, for the extension test in tests.py.
extension_demo = executable('otium-extension-demo', 'tests/unit/hosts/extension-demo.c',
  c_args: runtime_args, dependencies: libotium_dep, override_options: runtime_options)

ffi_extra = shared_module('ffi-extra', 'tests/unit/fixtures/ffi_extra.c',
  name_prefix: '', name_suffix: extension, build_by_default: false,
  override_options: runtime_options)

bench_runtime = executable('bench-runtime', 'bench/runtime.c', include_directories: 'src',
  c_args: runtime_args, dependencies: libotium_dep, override_options: runtime_options,
  build_by_default: false)
