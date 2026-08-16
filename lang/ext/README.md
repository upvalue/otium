# Native extensions

Extensions are optional C modules linked into the `otium` executable. They
are not linked into `libotium`, and there is no dynamic loading or stable C ABI
yet.

Extensions build by default (`-Dext_demo=true -Dext_ray=true`); pass `false` to
drop one, e.g. on a machine without raylib:

```sh
meson setup build -Dext_ray=false
```

Note that changed option defaults do not apply to an already-configured build
directory; update one with `meson configure build -Dext_ray=true`.

An extension registers a native module name. `(require 'demo)` creates and
enters the `demo` namespace, runs the native initializer once, restores the
caller's namespace, then looks for `demo.scm` on the normal load path. The
source file is optional. It is useful for wrappers and constants that do not
need to be compiled into the executable.

## Foreign objects

`src/heap.h` exposes the extension-facing API:

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

## Ray

The `ray` extension wraps Raylib and requires a system Raylib package
discoverable by `pkg-config`:

```sh
meson compile -C build
build/otium --path ext/ray ext/ray/example.scm
```

`ext/ray/roguelike.scm` is a larger example aimed at a grid game:

```sh
build/otium --path ext/ray ext/ray/roguelike.scm
```

It draws a Brogue-styled 60 by 30 glyph dungeon into a render texture: line of
sight with remembered tiles, torchlight falloff with flicker, per-tile color
jitter, animated water, and a status bar. The map scales and letterboxes when
the window changes size, movement is arrows, hjkl, or yubn diagonals, and the
per-tile path stays free of temporary Otium strings.

### Unattended runs (agents, CI)

The companion module ships a harness for driving a graphical script without a
human at the keyboard. Three environment variables control it:

- `RAY_FRAMES` — run this many frames, then stop the loop.
- `RAY_SCREENSHOT` — path to save a PNG capture of the final frame.
- `RAY_INPUT` — comma-separated tokens, one handed out per frame through
  `(ray/harness-next-input!)`; the roguelike maps `h,j,k,l,y,u,b,n` onto moves.

```sh
RAY_FRAMES=60 RAY_SCREENSHOT=shot.png RAY_INPUT=l,l,j \
  build/otium --path ext/ray ext/ray/roguelike.scm
```

A script opts in by calling `(ray/harness-continue?)` once per frame, after
drawing but before `end-drawing`, and looping while it returns true. The
capture is taken at that point so it sees the finished frame. `take-screenshot!`,
`env`, and `file-exists?` are also available directly for scripts that want a
different protocol.

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
- Utility: inclusive integer RNG with explicit seeding, screen capture to PNG,
  environment variable reads, and file-existence probes.
