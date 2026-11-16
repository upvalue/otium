# A prebuilt wgpu-native release: webgpu.h, wgpu.h and the shared library.
# The wrap for each platform points at that platform's archive.
project('wgpu-native', 'c', version: '29.0.1.1')
lib_dir = meson.current_source_dir() / 'lib'
wgpu_dep = declare_dependency(
  include_directories: include_directories('include'),
  link_args: ['-L' + lib_dir, '-lwgpu_native', '-Wl,-rpath,' + lib_dir])
