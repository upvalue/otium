project('otos', 'c',
  version: '0.1.0',
  meson_version: '>= 1.6',
  default_options: [
    'c_std=c23',
    'buildtype=debugoptimized',
    'warning_level=2',
    'b_staticpic=false',
  ])

picolibc = subproject('picolibc', default_options: [
  'multilib=false',
  'picocrt=false',
  'semihost=false',
  'tests=false',
  'format-default=double',
  'io-long-long=true',
  'newlib-global-errno=true',
  'thread-local-storage=false',
  'single-thread=true',
  'b_staticpic=false',
  'warning_level=0',
  'werror=false',
]).get_variable('picolibc_dep')

subdir('vendor/compiler-rt')

# The Otium C runtime from c/, built with OTIUM_FREESTANDING: no FFI, threads
# or POSIX file calls. The kernel's I/O host supplies the console and files.
# The bootstrap library is read from the embedded c/src/primitives.ot.
runtime_dir = '../c'
runtime_names = [
  'heap', 'reader', 'tokens', 'runtime', 'compiler', 'vm', 'builtins', 'numeric', 'time',
  'filesystem', 'process', 'ports', 'strings', 'bytevectors', 'reflection', 'support',
  'conditions', 'restarts', 'cleanup', 'params', 'classes', 'instances', 'records', 'foreign',
  'foreign_types', 'collections', 'tables', 'reclaim', 'modules']
runtime_sources = []
foreach name : runtime_names
  runtime_sources += files(runtime_dir / 'src' / name + '.c')
endforeach
# picolibc only defines CLOCK_MONOTONIC when the platform says it has one;
# the kernel's clock_gettime does.
platform_args = ['-D_POSIX_MONOTONIC_CLOCK=200809L']
runtime = static_library('otium', runtime_sources,
  c_args: platform_args + ['-DOTIUM_FREESTANDING',
    '-DOTIUM_BOOTSTRAP_FILE="/c/src/primitives.ot"', '-Wno-unused-parameter'],
  include_directories: include_directories(runtime_dir / 'include', runtime_dir / 'src'),
  dependencies: picolibc,
  override_options: ['c_std=c11'])

# lib/ and the bootstrap library from the language tree, readable at /lib and
# /c/src/primitives.ot inside the kernel.
embedded_files = custom_target('files.c',
  output: 'files.c',
  depfile: 'files.c.d',
  command: [find_program('python3'), files('tools/embed-files.py'),
            '@OUTPUT@', '@DEPFILE@', meson.current_source_dir() / '..',
            'c/src/primitives.ot', 'lib'])

linker_script = meson.current_source_dir() / 'src/kernel.ld'
executable('otos.elf',
  'src/boot.S', 'src/kernel.c', 'src/line.c', 'src/ot-kernel.c', 'src/virtio.c',
  'src/display.c', embedded_files,
  c_args: platform_args,
  include_directories: include_directories(runtime_dir / 'include', 'src'),
  dependencies: picolibc,
  link_with: [runtime, builtins],
  link_args: ['-T', linker_script, '-Wl,--gc-sections'],
  link_depends: linker_script)
