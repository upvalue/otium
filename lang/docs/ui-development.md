# UI development

The shared UI runs on C and Chez. It supports class-backed descriptions and
mounted state, Yoga layout, rounded rectangles on SDL_GPU, and pointer activation
of composed buttons. Text drawing, keyboard focus, scrolling and automatic file
watching are still pending.

## Build and run

The native build needs a C/C++ toolchain, CMake, pkg-config, Python 3.12 or later,
SDL3 3.4.12 or later, and `glslc`. Yoga sources are pinned and checksum-verified:

```sh
make ui-setup
make ui
```

For an offline Yoga install, use
`python3 shared/ui/setup.py --archive /path/to/yoga.tar.gz` instead of
`make ui-setup`. See `shared/ui/DEPENDENCIES.md` for the version and checksum.

Run either command from the repository root:

```sh
c/otium --no-project -L shared/lib -L examples examples/ui.scm
chez/run --no-project -L shared/lib -L examples examples/ui.scm
```

The window stays open until it is closed. Click any of the three colored cards
to toggle its color. They are composed widgets whose state survives rebuilding
the descriptions. The loop handles the left mouse button and window close;
it does not yet provide keyboard navigation or text input.

`examples/ui.scm` uses `otium.cli`. `--help` lists its options. Native library
paths come from `shared/build/ui-config.scm`, written by CMake and read as data.
`--config` selects another installation file. The application and scene modules
are checked-in Otium source.

## Bounded execution and captures

```sh
chez/run --no-project -L shared/lib -L examples examples/ui.scm -- \
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
Leaf measurement supplies an intrinsic size; constraint-dependent measurement
such as wrapped text is not implemented yet.

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
`ui/button` accepts children and `:on-press`; `ui/label` stores `:text` but does
not yet paint it. All constructors support library-owned `:key`. Strings become labels; lists and arrays
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

The implementation is `shared/lib/otium/ui.scm`; shared acceptance checks live
in `shared/tests/ui/checks.scm`. Runtime primitives in `otium.generic` provide
property construction, mounting and migration while preserving fixed slot
access. See `specs/classes.md` for their contracts.

## Layout, paint and pointer routing

`otium.ui.gpu/layout!` creates and reuses a Yoga node per mounted widget, then
stores absolute logical bounds. `ui/layout-style` supplies a table of layout
properties; `ui/measure` supplies a leaf's intrinsic `ui/Size`. `ui/paint` receives
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
