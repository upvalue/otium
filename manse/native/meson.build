# The reader pushes SDL events to wake the UI loop, so manse-native needs SDL.
if sdl3.found()
  manse_native = shared_library('manse-native', 'pty.c', 'reader.c',
    dependencies: [sdl3, dependency('threads')], override_options: native_options)
endif
