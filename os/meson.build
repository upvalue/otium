project('otium-os', ['c', 'cpp'],
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
    'optimization=0',  # No optimization by default, cross-files add -Os explicitly
  ])

# Detect target architecture
if meson.is_cross_build()
  target_arch = meson.get_cross_property('target_arch', 'unknown')
else
  target_arch = 'posix'
endif

message('Building for target:', target_arch)

# Generate config.h from template based on target
config_data = configuration_data()

# Kernel program mode from options
kernel_prog = get_option('kernel_prog')
kernel_prog_constant = 'KERNEL_PROG_' + kernel_prog.to_upper()
config_data.set('KERNEL_PROG', kernel_prog_constant)

# Log levels from options
log_levels = {
  'silent': 'LSILENT',
  'soft': 'LSOFT',
  'loud': 'LLOUD',
}
config_data.set('LOG_GENERAL', log_levels[get_option('log_general')])
config_data.set('LOG_MEM', log_levels[get_option('log_mem')])
config_data.set('LOG_PROC', log_levels[get_option('log_proc')])
config_data.set('LOG_IPC', log_levels[get_option('log_ipc')])

# Shell enable
config_data.set('SHELL_ENABLE', '#define ENABLE_SHELL')

# Platform-specific configuration
if target_arch == 'riscv'
  arch_defines = ['-DOT_ARCH_RISCV', '-DOT_FEAT_GFX=OT_FEAT_GFX_VIRTIO']
  is_posix = false
  is_riscv = true
  is_wasm = false
  default_graphics_backend = 'virtio'
  default_filesystem_backend = 'memory'
  default_keyboard_backend = 'virtio'
elif target_arch == 'wasm'
  arch_defines = ['-DOT_ARCH_WASM']
  is_posix = false
  is_riscv = false
  is_wasm = true
  default_graphics_backend = 'wasm'
  default_filesystem_backend = 'wasm'
  default_keyboard_backend = 'wasm'
elif target_arch == 'posix'
  arch_defines = ['-DOT_POSIX']
  is_posix = true
  is_riscv = false
  is_wasm = false
  default_graphics_backend = 'none'
  default_filesystem_backend = 'none'
  default_keyboard_backend = 'none'
else
  error('Unknown target architecture: ' + target_arch)
endif

# Picolibc for RISC-V builds
picolibc_dep = []
if is_riscv
  picolibc_proj = subproject('picolibc', default_options: [
    'multilib=false',
    'picocrt=false',           # We have our own startup code
    'picolib=false',
    'semihost=false',
    'tests=false',
    'tinystdio=true',          # Use tinystdio for printf
    'format-default=integer',  # Integer-only printf (no float, matches mpaland/printf config)
    'io-long-long=false',      # Match current printf config
    'io-float-exact=false',
    'newlib-global-errno=true',
    'thread-local-storage=false',
  ])
  picolibc_dep = picolibc_proj.get_variable('picolibc_dep')
  arch_defines += ['-DUSE_PICOLIBC']
endif

filesystem_backend = get_option('filesystem_backend')
if filesystem_backend == 'auto'
  filesystem_backend = default_filesystem_backend
endif

# Get backend options (use 'auto' to select platform default)
graphics_backend = get_option('graphics_backend')
if graphics_backend == 'auto'
  graphics_backend = default_graphics_backend
endif

keyboard_backend = get_option('keyboard_backend')
if keyboard_backend == 'auto'
  keyboard_backend = default_keyboard_backend
endif

# Set backend configuration
graphics_backend_map = {
  'none': 'OT_GRAPHICS_BACKEND_NONE',
  'test': 'OT_GRAPHICS_BACKEND_TEST',
  'virtio': 'OT_GRAPHICS_BACKEND_VIRTIO',
  'wasm': 'OT_GRAPHICS_BACKEND_WASM',
}

filesystem_backend_map = {
  'none': 'OT_FILESYSTEM_BACKEND_NONE',
  'memory': 'OT_FILESYSTEM_BACKEND_MEMORY',
  'onefile': 'OT_FILESYSTEM_BACKEND_ONEFILE',
  'fat': 'OT_FILESYSTEM_BACKEND_FAT',
  'wasm': 'OT_FILESYSTEM_BACKEND_WASM',
}

keyboard_backend_map = {
  'none': 'OT_KEYBOARD_BACKEND_NONE',
  'virtio': 'OT_KEYBOARD_BACKEND_VIRTIO',
  'wasm': 'OT_KEYBOARD_BACKEND_WASM',
}

config_data.set('GRAPHICS_BACKEND', graphics_backend_map[graphics_backend])
config_data.set('FILESYSTEM_BACKEND', filesystem_backend_map[filesystem_backend])
config_data.set('KEYBOARD_BACKEND', keyboard_backend_map[keyboard_backend])

# Generate config.h in the build directory under ot/ subdir
subdir('ot')

# Include directories
inc = include_directories('.')

# Common compile args
common_c_args = arch_defines + ['-Wno-unused-parameter', '-g3']
common_cpp_args = arch_defines + ['-Wno-unused-parameter', '-fno-exceptions', '-g3']

# =============================================================================
# POSIX builds (tcl-repl and tevl standalone tools)
# =============================================================================

if is_posix
  # Vendor library objects (C files)
  bestline_lib = static_library('bestline',
    'ot/vendor/bestline/bestline.c',
    c_args: common_c_args,
    include_directories: inc,
  )

  mpack_lib = static_library('mpack',
    'ot/vendor/libmpack/mpack.c',
    c_args: common_c_args,
    include_directories: inc,
  )

  printf_lib = static_library('printf',
    'ot/vendor/printf/printf.c',
    c_args: common_c_args,
    include_directories: inc,
  )

  # TCL REPL executable
  tcl_repl_sources = [
    'ot/user/string.cpp',
    'ot/user/tcl.cpp',
    'ot/lib/file.cpp',
    'ot/core/platform/tcl-repl.cpp',
    'ot/core/platform/posix-file.cpp',
    'ot/core/platform/posix-std.cpp',
    'ot/lib/std.cpp',
    'ot/lib/mpack/mpack-utils.cpp',
  ]

  executable('tcl-repl',
    tcl_repl_sources,
    cpp_args: common_cpp_args,
    include_directories: inc,
    link_with: [bestline_lib, mpack_lib, printf_lib],
    install: true,
  )

  # Text editor executable
  edit_sources = [
    'ot/user/string.cpp',
    'ot/lib/file.cpp',
    'ot/user/edit.cpp',
    'ot/user/tcl.cpp',
    'ot/lib/mpack/mpack-utils.cpp',
    'ot/core/platform/posix-file.cpp',
    'ot/core/platform/edit-termbox.cpp',
    'ot/core/platform/posix-std.cpp',
    'ot/lib/std.cpp',
  ]

  # Include vendor directory for termbox2
  vendor_inc = include_directories('vendor')

  executable('edit',
    edit_sources,
    cpp_args: common_cpp_args + ['-fsanitize=address', '-g3'],
    link_args: ['-fsanitize=address'],
    include_directories: [inc, vendor_inc],
    link_with: [printf_lib, mpack_lib],
    install: true,
  )

  # Unit tests executable
  unit_test_sources = [
    'ot/test-linking.cpp',
    'ot/core/memory-test.cpp',
    'ot/core/memory.cpp',
    'ot/core/std.cpp',
    'ot/lib/std.cpp',
    'ot/core/platform/posix-std.cpp',
    'ot/lib/printf-test.cpp',
    'ot/core/platform-test.cpp',
    'ot/core/process.cpp',
    'ot/core/process-test.cpp',
    'ot/lib/mpack/mpack-writer-test.cpp',
    'ot/lib/mpack/mpack-reader.cpp',
    'ot/lib/mpack/mpack-reader-test.cpp',
    'ot/lib/mpack/mpack-utils.cpp',
    'ot/lib/mpack/mpack-utils-test.cpp',
    'ot/lib/result-test.cpp',
    'ot/user/string.cpp',
    'ot/lib/string-test.cpp',
    'ot/lib/vector-test.cpp',
    'ot/user/tcl.cpp',
    'ot/user/tcl-test.cpp',
    'ot/user/edit.cpp',
    'ot/user/edit-test.cpp',
    'ot/core/platform/edit-test.cpp',
    'ot/core/platform/posix-file.cpp',
    'ot/lib/file.cpp',
  ]

  test_exe = executable('unit-test',
    unit_test_sources,
    cpp_args: common_cpp_args + ['-DOT_TEST', '-DOT_TRACE_MEM'],
    include_directories: inc,
    link_with: [mpack_lib, printf_lib],
    install: false,
  )

  # Register with Meson's test system
  test('unit tests', test_exe)
endif

# =============================================================================
# Kernel builds (RISC-V and WASM)
# =============================================================================

if is_riscv or is_wasm
  # Generate runtime configuration for run scripts
  runtime_config = configuration_data()
  runtime_config.set('GRAPHICS_BACKEND', graphics_backend_map[graphics_backend])
  runtime_config.set('FILESYSTEM_BACKEND', filesystem_backend_map[filesystem_backend])

  configure_file(
    output: 'runtime-config.sh',
    configuration: runtime_config,
    install: false,
    format: 'cmake',  # Use @VAR@ syntax
    input: files('tools/runtime-config.sh.in'),
  )

  # Generate shellrc header from fs-in/shellrc.tcl
  shellrc_header = custom_target(
    'shellrc-header',
    input: files('fs-in/shellrc.tcl'),
    output: 'shellrc.hpp',
    command: [
      find_program('python3'),
      files('tools/embed-file.py'),
      '@INPUT@',
      '@OUTPUT@',
      'shellrc_content'
    ],
  )

  # Shared source files for both RISC-V and WASM
  core_sources = [
    'ot/core/startup.cpp',
    'ot/core/main.cpp',
    'ot/core/kernel-tests.cpp',
    'ot/core/memory.cpp',
    'ot/core/process.cpp',
    'ot/core/std.cpp',
  ]

  lib_sources = [
    'ot/lib/std.cpp',
    'ot/lib/arena.cpp',
    'ot/lib/app-framework.cpp',
    'ot/lib/keyboard-utils.cpp',
    'ot/lib/mpack/mpack-reader.cpp',
    'ot/lib/mpack/mpack-utils.cpp',
    'ot/lib/file.cpp',
    'ot/lib/md5.cpp',
    'ot/vendor/libmpack/mpack.c',
    'ot/vendor/libschrift/schrift.c',
  ]

  # Only include mpaland/printf for non-picolibc builds (WASM)
  # RISC-V uses picolibc's tinystdio instead
  if not is_riscv
    lib_sources += ['ot/vendor/printf/printf.c']
  endif

  user_sources = [
    'ot/user/user-main.cpp',
    'ot/user/prog-echo.cpp',
    'ot/user/prog-scratch.cpp',
    'ot/user/prog-gfxscratch.cpp',
    'ot/user/prog/shell/commands.cpp',
    'ot/user/prog/shell/textshell.cpp',
    'ot/user/prog/shell/uishell.cpp',
    'ot/user/prog-spacedemo.cpp',
    'ot/user/prog-typedemo.cpp',
    'ot/user/prog/editor/uieditor.cpp',
    'ot/user/edit.cpp',
    'ot/user/tcl.cpp',
    'ot/user/string.cpp',
    'ot/user/memory-allocator.cpp',
    'ot/user/comm-writer.cpp',
    'ot/vendor/tlsf/tlsf.c',
    # Generated IPC code
    'ot/user/gen/fibonacci-client.cpp',
    'ot/user/gen/fibonacci-server.cpp',
    'ot/user/fibonacci/impl.cpp',
    'ot/user/gen/graphics-client.cpp',
    'ot/user/gen/graphics-server.cpp',
    'ot/user/graphics/impl.cpp',
    'ot/user/gen/filesystem-client.cpp',
    'ot/user/gen/filesystem-server.cpp',
    'ot/user/gen/keyboard-client.cpp',
    'ot/user/gen/keyboard-server.cpp',
    'ot/user/keyboard/impl.cpp',
    'ot/lib/frame-manager.cpp',
    # 'ot/user/filesystem/impl-memory.cpp',
  ]

  # Filesystem implementation
  if filesystem_backend == 'memory'
    user_sources += ['ot/user/fs/impl-memory.cpp']
  elif filesystem_backend == 'onefile'
    user_sources += [
      'ot/user/fs/impl-onefile.cpp',
      'ot/user/fs/virtio-disk.cpp',
    ]
  elif filesystem_backend == 'fat'
    user_sources += [
      'ot/user/fs/impl-fat.cpp',
      'ot/user/fs/virtio-disk.cpp',
      'ot/user/fs/fatfs-diskio.cpp',
      'ot/vendor/fatfs/ff.c',
    ]
  elif filesystem_backend == 'wasm'
    user_sources += ['ot/user/fs/impl-wasm.cpp']
  endif

  # Platform-specific sources and configuration
  if is_riscv
    graphics_sources = ['ot/user/graphics/backend-virtio.cpp']
    keyboard_sources = []
    if keyboard_backend == 'virtio'
      keyboard_sources += ['ot/user/keyboard/backend-virtio.cpp']
    endif
    file_backend_sources = ['ot/core/platform/ipc-file.cpp']
    platform_sources = [
      'ot/core/platform/platform-riscv.cpp',
      'ot/lib/platform/shared-riscv.cpp',
      'ot/core/platform/user-riscv.cpp',
    ]
    all_sources = core_sources + lib_sources + user_sources + graphics_sources + keyboard_sources + file_backend_sources + platform_sources
    linker_script = meson.current_source_dir() / 'kernel-link-riscv.ld'

    executable('os.elf',
      all_sources + [shellrc_header],
      c_args: common_c_args,
      cpp_args: common_cpp_args,
      link_args: [
        '-Wl,-T' + linker_script,
        '-Wl,-Map=@0@/os.map'.format(meson.current_build_dir()),
      ],
      include_directories: inc,
      dependencies: picolibc_dep,
      install: true,
    )
  endif

  if is_wasm
    file_backend_sources = ['ot/core/platform/ipc-file.cpp']
    platform_sources = [
      'ot/core/platform/platform-wasm.cpp',
      'ot/lib/platform/shared-wasm.cpp',
      'ot/core/platform/user-wasm.cpp',
    ]
    all_sources = core_sources + lib_sources + user_sources + file_backend_sources + platform_sources

    # WASM graphics backend is inline, no separate source file needed
    executable('os',
      all_sources + [shellrc_header],
      c_args: common_c_args,
      cpp_args: common_cpp_args,
      include_directories: inc,
      install: true,
      name_suffix: 'js',  # Emscripten generates os.js and os.wasm
    )
  endif
endif
