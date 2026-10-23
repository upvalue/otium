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

# The Otium C runtime, compiled from the language tree with a kernel
# platform layer in place of ot-posix.c.
runtime_dir = '../lang/c/src'
runtime_args = [
  '-DOT_GC_SEMI',
  '-DOT_HEAP_INIT=1048576',
  '-DOT_HEAP_MAX=16777216',
  '-DOT_MAX_DEPTH=10000',
  '-DOT_GC_TIMING=0',
  '-Wno-unused-parameter',
]
runtime = static_library('otium',
  files(runtime_dir / 'otium.c', runtime_dir / 'ot-gc-common.c', runtime_dir / 'ot-gc-semi.c',
        runtime_dir / 'ot-ffi.c', runtime_dir / 'ot-cli.c'),
  c_args: runtime_args,
  dependencies: picolibc)

linker_script = meson.current_source_dir() / 'src/kernel.ld'
executable('otos.elf',
  'src/boot.S', 'src/kernel.c', 'src/ot-kernel.c',
  c_args: runtime_args,
  include_directories: include_directories(runtime_dir),
  dependencies: picolibc,
  link_with: [runtime, builtins],
  link_args: ['-T', linker_script, '-Wl,--gc-sections'],
  link_depends: linker_script)
