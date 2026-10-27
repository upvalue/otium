# The C runtime. See docs/runtime-rewrite.md.

# The runtime's sources are listed in sources.list, which otos also reads.
runtime_names = []
foreach entry : fs.read('sources.list').strip().split('\n')
  if not entry.startswith('#')
    runtime_names += entry
  endif
endforeach
runtime_sources = []
foreach name : runtime_names
  runtime_sources += files('src' / name + '.c')
endforeach

# The runtime is C11 with GNU builtins and computed goto, so it skips the
# project's C23 and -Wpedantic.
runtime_headers = files('src/internal.h', 'src/heap.h', 'src/reader.h', 'src/foreign.h',
  'src/foreign_types.h', 'src/diagnostic-catalog.inc')
runtime_options = ['c_std=c11', 'warning_level=2']
# The amalgamation writes otium.h into this build directory, so targets that use
# c/include must not search it.
no_implicit = {'implicit_include_directories': false}
runtime_include = include_directories('include', 'src')
runtime_deps = [dependency('libffi'), dependency('threads'), dl, libm]
runtime_args = [
  '-DOTIUM_LIBRARY_DIR="@0@"'.format(source_root / 'lib'),
  '-DOTIUM_BOOTSTRAP_FILE="@0@"'.format(source_root / 'c/src/primitives.ot'),
  '-DOTIUM_DEV_FILE="@0@"'.format(source_root / 'c/src/dev.ot'),
  '-DOTIUM_TEST_FIXTURE_DIR="@0@"'.format(build_root),
  '-DOTIUM_TEST_LIBRARY_EXTENSION="@0@"'.format(extension),
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

# Position-independent code costs a few percent in the VM, and the runtime is
# only linked statically. The development server's socket transport is shared
# with Chez.
libotium = static_library('otium', runtime_sources,
  c_args: runtime_args, include_directories: [runtime_include, native_include],
  dependencies: runtime_deps, link_whole: dev.get_static_lib(), override_options: runtime_options,
  pic: false, kwargs: no_implicit)
libotium_dep = declare_dependency(link_with: libotium,
  include_directories: include_directories('include'), dependencies: runtime_deps)

# The single-file distribution: otium.h, otium.c and main.c.
amalgamation = custom_target('amalgamation',
  input: runtime_sources, output: ['otium.c', 'otium.h', 'main.c'],
  command: [python, files('tools/amalgamate.py'), '@OUTDIR@', '@INPUT@'],
  depend_files: files('include/otium.h', 'src/main.c', 'src/options.inc') + runtime_headers)
otium_amalgamation = executable('otium-amalgamation', amalgamation[0], amalgamation[2],
  c_args: runtime_args, include_directories: native_include,
  link_with: [dev.get_static_lib(), line.get_static_lib()], dependencies: runtime_deps,
  override_options: runtime_options, build_by_default: false)

# Each tests/unit/NAME.c is one suite, and all of them link into one runner.
# value.c is built a second time with the portable integer representation.
unit_suites = [
  'bytevectors', 'cleanup', 'conditions', 'control', 'defonce', 'evaluation', 'file-ports',
  'filesystem', 'foreign', 'foreign-reload', 'generics', 'handlers', 'heap', 'heap-growth',
  'instances', 'interrupt', 'io-host', 'macros', 'methods', 'modules', 'namespaces', 'numeric',
  'optimizer',
  'params', 'port-reader', 'port-shutdown', 'ports', 'process', 'properties', 'quasiquote',
  'reader', 'records', 'reflection', 'reload', 'restarts', 'runtime', 'scope-roots',
  'shared-expander', 'slot-reuse', 'strings', 'time', 'tokens', 'value']
unit_include = include_directories('tests/unit/support', 'src')
unit_support = static_library('otium-test-support',
  'tests/unit/support/test.c', 'tests/unit/support/helpers.c',
  c_args: runtime_args, include_directories: unit_include, dependencies: libotium_dep,
  override_options: runtime_options,
  kwargs: no_implicit)
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
  dependencies: libotium_dep, override_options: runtime_options,
  kwargs: no_implicit)
# macOS hides mkdtemp under _POSIX_C_SOURCE unless _DARWIN_C_SOURCE is set.
unit_args = runtime_args
if host_machine.system() == 'darwin'
  unit_args += '-D_DARWIN_C_SOURCE'
endif
unit_tests = executable('otium-unit-tests', unit_main, unit_sources,
  c_args: unit_args, include_directories: unit_include, dependencies: libotium_dep,
  link_with: [unit_support, value_portable], override_options: runtime_options,
  kwargs: no_implicit)

# A host program for the callback abort checks in tests.py.
foreign_control = executable('otium-foreign-control', 'tests/unit/hosts/foreign-control.c',
  c_args: runtime_args, include_directories: unit_include, dependencies: libotium_dep,
  link_with: unit_support, override_options: runtime_options,
  kwargs: no_implicit)

# A host with native procedures, for the extension test in tests.py.
extension_demo = executable('otium-extension-demo', 'tests/unit/hosts/extension-demo.c',
  c_args: runtime_args, dependencies: libotium_dep, override_options: runtime_options,
  kwargs: no_implicit)

ffi_extra = shared_module('ffi-extra', 'tests/unit/fixtures/ffi_extra.c',
  name_prefix: '', name_suffix: extension, build_by_default: false,
  override_options: runtime_options,
  kwargs: no_implicit)

bench_runtime = executable('bench-runtime', 'bench/runtime.c', include_directories: 'src',
  c_args: runtime_args, dependencies: libotium_dep, override_options: runtime_options,
  build_by_default: false,
  kwargs: no_implicit)
