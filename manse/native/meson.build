# The reader pushes SDL events to wake the UI loop, so manse-native needs SDL.
# libghostty-vt is optional: without it, Manse walks terminal cells from Otium.
if sdl3.found()
  manse_ghostty = dependency('libghostty-vt', required: false)
  manse_native = shared_library('manse-native', 'pty.c', 'reader.c', 'cells.c',
    c_args: manse_ghostty.found() ? ['-DMANSE_GHOSTTY'] : [],
    dependencies: [sdl3, dependency('threads'), manse_ghostty], override_options: native_options)
endif
