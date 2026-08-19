# Examples

`hand/` contains code written by people. Agents may copy it when reorganizing
the tree, but should not edit it. The other directories are maintained examples.

## Native extensions

Extensions are optional C modules linked into the `otium` executable. They
are not linked into `libotium`, and there is no dynamic loading or stable C ABI
yet.

The demo extension always builds. The Raylib extension builds when `pkg-config`
finds Raylib. Override detection in `site.mk` when needed:

```sh
WITH_RAY = 0
```

An extension registers a native module name. `(require 'demo)` creates and
enters the `demo` namespace, runs the native initializer once, restores the
caller's namespace, then looks for `demo.ot` on the normal load path. The
source file is optional. It is useful for wrappers and constants that do not
need to be compiled into the executable.

## Extension values

`src/otium.h` exposes the extension-facing API:

- `ot_ext_type` registers a per-state type and optional finalizer.
- `ot_ext_inline` stores a small POD value in the moving heap object.
- `ot_ext_pointer` stores an external pointer owned by the object.
- `ot_ext_check` validates the type and live flag, returning a condition
  for a wrong or released value.
- `ot_ext_release` finalizes once and marks the value released.

Inline bytes move safely with the Cheney collector. Extension payloads cannot
contain Otium `Value`s because there is no extension trace callback in this
version. Finalizers run during collection and must not allocate on the Otium
heap or re-enter evaluation.

C functions use the same `OT_FRAME` rooting discipline as the builtins. A
raw heap `otv` cannot live across an allocating call.

`examples/demo` is the small reference implementation. It has both an inline
counter and a malloc-owned counter, plus explicit release behavior.

## Ray

The `ray` extension wraps Raylib and requires a system Raylib package
discoverable by `pkg-config`:

```sh
make
build/otium examples/ray/example.scm
```

`examples/ray/roguelike.ot` is a larger example aimed at a grid game:

```sh
build/otium examples/ray/roguelike.ot
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
  build/otium examples/ray/roguelike.ot
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

Textures, fonts, and render textures are owned extension handles. Call
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
