# UI development

The shared UI runs on C and Chez. It supports class-backed descriptions and
mounted state, constraint-aware Yoga layout, rounded rectangles and shaped text
on SDL_GPU, and pointer activation of composed buttons. Keyboard focus, scrolling
and automatic file watching are still pending.

## Build and run

The native build needs a C/C++ toolchain, CMake, pkg-config, Python 3.12 or later,
SDL3 3.4.12 or later, SDL3_ttf 3.2.2 or later (`sdl3-ttf` in pkg-config),
and `glslc`. Yoga sources are pinned and checksum-verified:

```sh
make ui-setup
make ui
```

For an offline Yoga install, use
`tools/python shared/ui/setup.py --archive /path/to/yoga.tar.gz` instead of
`make ui-setup`. See `shared/ui/DEPENDENCIES.md` for the version and checksum.

Run either command from the repository root:

```sh
c/otium --no-project -L shared/lib -L examples examples/ui.ot
chez/run --no-project -L shared/lib -L examples examples/ui.ot
```

The window stays open until it is closed. Click any of the three colored cards
to toggle its color. Labels use bundled Noto Sans and JetBrains Mono; the body
paragraph wraps at the available width. Try `--width 480` to see it reflow.
The cards are composed widgets whose state survives rebuilding
the descriptions. The loop handles the left mouse button and window close;
it does not yet provide keyboard navigation or text input.

`examples/ui.ot` uses `otium.cli`. `--help` lists its options. Native library
paths come from `shared/build/ui-config.ot`, written by CMake and read as data.
`--config` selects another installation file. The application and scene modules
are checked-in Otium source.

## Bounded execution and captures

```sh
chez/run --no-project -L shared/lib -L examples examples/ui.ot -- \
  --headless --frames 3 --capture /tmp/ui.png --dump-state /tmp/ui.json
```

`--headless` hides the window and exits after `--frames` frames (default 1).
It still needs a working SDL video/GPU backend. Captures use PNG or BMP; the
JSON output contains logical bounds, actual frame count, backend, and remaining
resource counts. `--scale 2` doubles capture resolution without changing logical
layout. Interactive mode saves a requested capture when the window closes.

`--scene ui.layout` chooses a module exporting `(view)`. `--script MODULE` loads
a module exporting `(step! tree frame)`, called before each reconciliation.
Add its directory with the interpreter's `-L` option. Test scripts can change
state, route pointer events, or reload a checked-in module at a known frame.
They need no source generation or interactive REPL session.

```sh
make test-ui
make test-ui-layout
make test-ui-gpu
```

The first two run without a display, including C GC stress checks. GPU checks
capture scenes on both runtimes and compare bounds and pixels, including scale,
clipping, alpha blending, rounded edges, button activation and method reload.
They also check teardown after errors. Captures remain in
`shared/build/ui-test-artifacts/`. Python invokes the checked-in applications
and checks their output; it does not generate Otium programs.

Metal on macOS has been exercised. Vulkan shaders compile, but the renderer has
not been validated on Linux. The renderer currently redraws the whole tree.
Leaf measurement receives Yoga's actual constraints, including wrapped text.

## Driving a widget tree

Require `(otium.ui :as ui)`, create a root with `(ui/root)`, and call
`(ui/reconcile! root (view))` after changing data or sending an event. Constructors
return descriptions; `(ui/children node)` returns an array of mounted child
nodes, and `(ui/instance node)` returns the persistent widget instance.
`(ui/inspect node)` returns its instance, key, active flag, bounds and children.

`(ui/send! node {:kind :press})` activates a headless button. Custom widgets
can implement `ui/handle-event` for other synthetic event tables. Dispatch does
not automatically rebuild the tree. The caller controls when to reconcile,
which makes a short event sequence deterministic and easy to inspect.

`ui/row` and `ui/column` accept `:width`, `:height`, `:grow`, `:padding`,
`:gap`, `:background` and `:radius`. Width and height default to automatic size.
`ui/button` accepts children and `:on-press`. `ui/label` paints its text and
accepts `:font` (`:ui` or `:mono`), `:size` (1–256 logical pixels), `:color`,
`:wrap` (default `#t`), `:width`, `:height`, and `:grow`. Font and color are
explicit label properties; they are not inherited from containers. All constructors support library-owned `:key`. Strings become labels; lists and arrays
flatten recursively; `nil` and empty lists are skipped. `ui/build` defaults to
returning constructor-supplied children. Custom methods return children directly.

## Identity, state and reload

A class descending from `ui/Widget` can mark slots `:state #t`. State slots must
have initializers and cannot be supplied in `ui/new` options. Description
construction evaluates only property defaults. Mounting initializes state;
matching updates properties while preserving state.

Keys are keywords, symbols, strings or integers and must be unique among
siblings. Keyed widgets match by key and class identity. Unkeyed widgets match
by position and class identity. Removing a widget deactivates its node and
descendants; `ui/send!` then rejects those nodes. Reintroducing the same key
creates fresh state. Retained application closures can still hold an old
instance; removal does not revoke arbitrary closures.

Reload classes and methods, then construct fresh descriptions and reconcile.
Same-name state survives; new state initializes; removed state disappears.
Changing a slot between property and state resets it. A newly required property
can be supplied by the new description during migration. Old descriptions are
rejected after their widget class layout changes.

A failed class migration restores that instance's layout and slots and can be
retried. Reconciliation is not a transaction across the whole tree: property
updates and application side effects before a failing `build` remain. Fix the
method and reconcile again. The future event loop must report such failures
and let the caller retry. `(ui/dispose! tree)` releases mounted Yoga nodes;
the CLI releases its renderer and window even when a frame raises an error.

The implementation is `shared/lib/otium/ui.ot`; shared acceptance checks live
in `shared/tests/ui/checks.ot`. Runtime primitives in `otium.generic` provide
property construction, mounting and migration while preserving fixed slot
access. See `specs/classes.md` for their contracts.

## Layout, paint and pointer routing

`otium.ui.gpu/layout!` creates and reuses a Yoga node per mounted widget, then
stores absolute logical bounds. `ui/layout-style` supplies a table of layout
properties; `ui/measure` supplies a leaf's `ui/Size` for the supplied constraints. `ui/paint` receives
a drawing context and bounds. `otium.gfx/rect` records a rounded rectangle with
an `0xRRGGBBAA` color. Painting visits parents before children and intersects
each child's rectangular clip with its ancestors.

`otium.ui.input` handles routing without depending on SDL. Create an
`input/pointer`, then call `input/dispatch!` with the tree and an event containing
`:kind`, `:x` and `:y`. Pointer kinds are `:pointer-down` and `:pointer-up`.
`input/cancel!` handles lost window focus. Coordinates are logical pixels.

Hit testing starts with the last painted sibling, clips to ancestor bounds,
and bubbles from the deepest child until a handler returns `:handled`. The
handler captures the press. Release activates it through `:press` only when
it is still mounted and the release hits that node or one of its descendants.
Release outside, removal, or cancellation prevents activation. Rounded corners
currently use the widget's rectangular hit region. Drag, hover and keyboard
focus are pending.

The native SDL adapter copies button coordinates into scalar values for Otium;
managed widgets never receive an SDL event pointer. The demo polls these events,
reconciles and paints, and delays between interactive frames. It supports one
fixed-size window; the general window/reload service described in the spec is
still planned.

## Text and measurement

`ui/measure` receives `:width-mode` and `:height-mode`: `:undefined`, `:exactly`,
or `:at-most`. A defined axis also has a numeric `:width` or `:height` in logical
pixels. The native bridge clamps the returned size to those modes. Yoga can skip
measurement when both dimensions are fixed. A custom measure method should be
pure: it may be called several times with different limits during one layout.

The FFI does not call back into either interpreter. Yoga records unanswered
measurement requests, Otium calls `ui/measure` between passes, then Yoga runs
again with the answers. All measured leaves are dirtied between passes so cached
placeholder sizes cannot survive. Answers are discarded on the next `layout!`,
which observes changed properties and redefined methods. Requests are bounded
at 256 per leaf and replay at 256 passes; exhaustion raises an error. Failed
measurement releases the temporary root and can be retried.

SDL_ttf shapes and wraps text. A custom `TTF_TextEngine` exposes glyph operations;
it does not draw through SDL's built-in renderers. Glyph bitmaps enter a renderer-
owned 2048×2048 RGBA atlas with transparent gutters. Glyphs and rectangles share
one ordered instance buffer, draw call, and clipping path on Metal and Vulkan.
Atlas uploads happen only when a new glyph is added. A bounded cache retains 128
shaped runs; up to 64 font/size combinations remain alive while trees or renderers
can refer to them. Disposing the last tree and renderer frees the fonts and runs.
The atlas holds at most 8192 glyphs and raises an error when full; eviction and
multiple atlas pages are deferred.

`gfx/text` takes `(g bounds string font size color wrap)`. Wrapping uses the
integer floor of the available width, with a minimum of one pixel; Yoga retains
fractional bounds and handles zero-width clipping. With `wrap` false, only
explicit newlines break lines. Glyphs use grayscale alpha and the supplied color.
Current fonts cover their own character sets; fallback chains, color emoji,
font inheritance and text alignment are pending. Glyphs are measured and rasterized at the same physical pixel scale, and glyph
origins are snapped to physical pixels. Interactive windows use their initial
display density; headless runs default to scale 1. `--scale` overrides this.
Moving a window between displays with different densities is not handled yet.

The checked-in text probe and layout fixtures cover constraint modes, changed
width/text/size, Unicode, explicit newlines, empty text, zero width, clipping,
alpha, runtime capture parity and font/run teardown. The display tests also
exercise repeated frames so atlas reuse is covered.

## Application identity

The demo accepts `--app-name`, `--app-id`, `--app-version`, `--title`, and `--icon`
(PNG or BMP). Interactive launch requests foreground focus; `--background` skips
that request. `gpu/configure-app!` sets metadata before SDL video initialization,
and `gpu/present-window!` installs the icon and requests focus.

macOS takes the app-switcher name from the executable's bundle. For development,
run either runtime through the bundle launcher:

```sh
tools/python shared/ui/run_app.py --runtime chez --app-name "Otium UI"
tools/python shared/ui/run_app.py --runtime c --app-name "Calculator" -- --scene ui.layout
```

The launcher copies the host executable (and Chez boot files) into an ignored
bundle under `shared/build/apps/`, then runs the checked-in application. It does
not generate Otium source. This supplies development process identity; it is not
a standalone distributable application. Direct interpreter launches still work,
but their app-switcher name may remain the interpreter's name.

## Button and focus checkpoint

Buttons now have padding, centered labels, plain/accent/bare styles, and paint
support for pressed and focused states. Mounted trees hold focus independently
of widget properties. `ui/focus!` and `ui/focused` expose that state. Common
`:disabled`, `:focusable`, `:keymap`, and `:on-action` options are library-owned.
Keyboard routing and the application demo are the next step; this checkpoint
does not yet wire Tab or Enter/Space to the native event loop.
