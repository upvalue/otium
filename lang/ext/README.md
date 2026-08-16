# Native extensions

Extensions are optional C++ modules linked into the `otium` executable. They
are not linked into `libotium`, and there is no dynamic loading or stable C ABI
yet.

The default build has no extensions. Enable the dependency-free demo with:

```sh
meson setup build-demo -Dext_demo=true
meson compile -C build-demo
meson test -C build-demo
build-demo/otium --path ext/demo tests/ext-demo.scm
```

An extension registers a native module name. `(require 'demo)` creates and
enters the `demo` namespace, runs the native initializer once, restores the
caller's namespace, then looks for `demo.scm` on the normal load path. The
source file is optional. It is useful for wrappers and constants that do not
need to be compiled into the executable.

## Foreign objects

`src/heap.hpp` exposes the extension-facing API:

- `register_foreign_type` registers a per-VM type name and optional finalizer.
- `make_foreign_inline` stores a small POD value in the moving heap object.
- `make_foreign_pointer` stores an external pointer owned by the Foreign object.
- `foreign_check` validates the type and live flag, returning an Otium condition
  for a wrong or released value.
- `foreign_release` finalizes once and marks the value released.

Inline bytes move safely with the Cheney collector. Foreign payloads cannot
contain Otium `Value`s because there is no extension trace callback in this
version. Finalizers run during collection and must not allocate on the Otium
heap or re-enter evaluation.

Native functions use the same `Slot`, `Scope`, and `ARG` rooting discipline as
the builtins. A raw heap `Value` cannot live across an allocating call.

`ext/demo` is the small reference implementation. It has both an inline
counter and a malloc-owned counter, plus explicit release behavior.

## Raylib

The Raylib extension requires a system Raylib package discoverable by
`pkg-config`:

```sh
meson setup build-raylib -Dext_raylib=true
meson compile -C build-raylib
meson test -C build-raylib
build-raylib/otium --path ext/raylib ext/raylib/example.scm
```

`ext/raylib/roguelike.scm` is a larger example aimed at a grid game:

```sh
build-raylib/otium --path ext/raylib ext/raylib/roguelike.scm
```

It draws a 60 by 30 glyph map into a render texture, scales and letterboxes it
when the window changes size, handles arrow or hjkl movement, and keeps the
per-tile path free of temporary Otium strings.

The main loop stays in Otium. Calls such as `begin-drawing`, `draw-circle`, and
`end-drawing` are thin native leaves, so the evaluator checks its interrupt
flag between calls.

Colors are packed `0xRRGGBBAA` integers. The companion module defines `rgb`,
`rgba`, common colors, keys, and mouse buttons. Positions and rectangles use
flat numeric arguments instead of allocating temporary vector objects.

Textures, fonts, and render textures are inline Foreign handles. Call
`unload-texture!`, `unload-font!`, or `unload-render-texture!` while the window
is still open. Their GC finalizers are a backstop and do nothing after the
graphics context has closed. Using an explicitly unloaded handle raises an
Otium condition.

The current native surface includes:

- Window and frame control: init flags, resize and fullscreen state, frame
  timing, FPS, and logical or render dimensions.
- Immediate-mode drawing: `begin-drawing`, `end-drawing`, `clear-background`,
  text, codepoints, rectangles, circles, lines, and scissor regions.
- Input: key state and queues, Unicode character input, mouse position, mouse
  buttons, and mouse wheel movement.
- Resources: texture regions, owned or default fonts, text measurement, and
  scaled render-texture presentation.
- Utility: inclusive integer RNG with explicit seeding.
