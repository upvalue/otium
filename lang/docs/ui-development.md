# UI development

The shared UI runs on C and Chez. It supports class-backed descriptions and
mounted state, constraint-aware Yoga layout, rounded rectangles and shaped text
on SDL_GPU, composed buttons, editable text fields, vertical scrolling,
keyboard focus and action routing. Automatic file watching is still pending.

## Build and run

The UI needs a C/C++ toolchain, Meson, pkg-config, Python 3.12 or later for
its tests, SDL3 3.4.12 or later, SDL3_ttf 3.2.2 or later (`sdl3-ttf` in
pkg-config), libffi and `glslc`. `just build` builds it when they're installed;
Meson downloads the pinned Yoga sources on first configure. See
[native components](native.md#dependencies) for versions and checksums.

Run either command from the repository root:

```sh
./otium --runtime=c examples/ui.ot
./otium --runtime=chez examples/ui.ot
```

The window stays open until it is closed. Click any of the three colored cards
to toggle its color. Labels use bundled Noto Sans and JetBrains Mono; the body
paragraph wraps at the available width. Try `--width 480` to see it reflow.
The cards are composed widgets whose state survives rebuilding
the descriptions. The loop handles the left mouse button and window close;
Tab and Shift+Tab move focus; Enter or Space activate the focused button on
release. The terminal scene exercises text input and scrolling.

`examples/ui.ot` uses `otium.cli`. `--help` lists its options. Native library
paths come from `build/ui-config.ot`, written by Meson and read as data.
`--config` selects another installation file. The application and scene modules
are checked-in Otium source.

## Bounded execution and captures

```sh
./otium --runtime=chez examples/ui.ot -- \
  --headless --frames 3 --capture /tmp/ui.png --dump-state /tmp/ui.json
```

`--headless` hides the window and exits after `--frames` frames (default 1).
It still needs a working SDL video/GPU backend. Captures use PNG or BMP; the
JSON output contains logical bounds, focus/pressed/disabled flags, actual frame count, backend, and remaining
resource counts. `--scale 2` doubles capture resolution without changing logical
layout. Interactive mode saves a requested capture when the window closes.

`--scene ui.layout` chooses a module exporting `(view)`. `--script MODULE` loads
a module exporting `(step! tree frame context)`, called before frame reconciliation.
The context supplies the actual `:window` and `:renderer` handles and an
`:on-cleanup` function for registering cleanup callbacks after SDL shutdown.
Add its directory with the interpreter's `-L` option. Test scripts can change
state, route pointer events, or reload a checked-in module at a known frame.
They need no source generation or interactive REPL session.

```sh
just test ui
just test 'ui layout'
just test 'ui gpu'
```

The first two run without a display. GPU checks
capture scenes on both runtimes and compare bounds and pixels, including scale,
clipping, alpha blending, rounded edges, button activation and method reload.
They also check teardown after errors. Captures remain in
`build/ui-test-artifacts/`. Python invokes the checked-in applications
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
`:padding-x`, `:padding-y`, `:gap`, `:align-items`, `:background`, `:border`
and `:radius`. Width and height default to automatic size. `:padding-x` and
`:padding-y` override `:padding` on one axis. `:align-items` places children
across the main axis: `:stretch` (default), `:start`, `:center` or `:end`.
`:border` paints a one-pixel border in the given colour; without a background
its corners are square.
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

The implementation is `lib/otium/ui.ot`; shared acceptance checks live
in `tests/ui/checks.ot`. Runtime primitives in `otium.generic` provide
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
`input/cancel!` cancels pointer capture. Coordinates are logical pixels.

Hit testing starts with the last painted sibling, clips to ancestor bounds,
and bubbles from the deepest child until a handler returns `:handled`. The
handler captures the press. Release activates it through `:press` only when
it is still mounted and the release hits that node or one of its descendants.
Release outside, removal, or cancellation prevents activation. Rounded corners
currently use the widget's rectangular hit region. Captured pointer motion is
delivered as `:pointer-drag`; text fields use it for selection.

Pointer motion marks every node on the hit path as hovered, so a container is
hovered while the pointer is over its children. Paint methods read
`(g :hovered?)`. The window refreshes hover after each layout, so content that
scrolls or changes under a still pointer updates it. `:pointer-leave` clears it.

The native SDL adapter copies button coordinates into scalar values for Otium;
managed widgets never receive an SDL event pointer. `native/poll-event` returns
an event table with a keyword `:kind`, or `nil` when the queue is empty. Payloads
are copied before the next poll and only contain fields for that event kind.
Unknown native kinds are errors. Motion is `:pointer-move`; the input router
delivers `:pointer-drag` only to a valid captured widget. The demo polls these events,
reconciles and paints, and delays between interactive frames. It supports one
resizable window; the general window/reload service described in the spec is
still planned.

Named constants use `UPPER_SNAKE_CASE`, including `sdl/INIT_VIDEO`,
`sdl/WINDOW_HIDDEN`, and the native layout field selectors. Event kinds remain
keywords such as `:key-down`. Test event injection lives in `ui.events` and a
small C fixture compiled against SDL headers. Tests use key names and the actual
window handle, without numeric keycodes, guessed struct offsets, or title lookup.

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
font inheritance are pending. Labels support `:align :left`, `:center`, or `:right`.
Glyphs are measured and rasterized at the same physical pixel scale, and glyph
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
python3 tools/ui-app.py --runtime chez --app-name "Otium UI"
python3 tools/ui-app.py --runtime c --app-name "Calculator" -- --scene ui.layout
```

The launcher copies the host executable (and Chez boot files) into an ignored
bundle under `build/apps/`, then runs the checked-in application. It does
not generate Otium source. This supplies development process identity; it is not
a standalone distributable application. Direct interpreter launches still work,
but their app-switcher name may remain the interpreter's name.

## Buttons, keyboard input and calculator

Buttons now have padding, centered labels, plain/accent/bare styles, and paint
support for pressed and focused states. Mounted trees hold focus independently
of widget properties. `ui/focus!` and `ui/focused` expose that state. Common
`:disabled`, `:focusable`, `:keymap`, and `:on-action` options are library-owned.
Disabled ancestors disable their descendants. Tab traverses enabled focusable
nodes in tree order and wraps; Shift+Tab reverses direction. Pointer presses
focus the capturing button. Enter and Space show its pressed state and activate
once on matching keyup. Button repeat keydowns are ignored. Focus change, window focus
loss, disable and unmount cancel pending keyboard activation.

`input/keyboard!` accepts `:key-down`, `:key-up`, and `:focus-lost` events.
Key events contain `:key` strings and optional `:repeat`. Native events also
carry a physical `:code` used to match keyup even if modifiers changed while
the key was held. Synthetic events can omit it to match by `:key`. Native keys use SDL's
names (`"Return"`, `"Space"`, `"Escape"`, uppercase letters), prefixed in order
with `Ctrl+`, `Alt+`, `Meta+`, and `Shift+` when held. Tab and focused-button
activation take precedence over keymaps. Other keydowns search from the focused
node through its ancestors; without focus they search root children. A keymap
maps strings to action values. Its `:on-action` receives the action and returns
`:handled` to stop routing. Unhandled keys bubble through `ui/handle-event`.
These options live on mounted nodes, so custom widgets need no common slots.

Custom widgets control input through four methods: `ui/default-focusable?`
and `ui/accepts-text?` take the widget; `ui/repeat-key?` and
`ui/activation-key?` also take the key event. The base `Widget` returns false
for all four. Buttons opt into focus and Return/Space activation. Text fields
opt into focus, committed text, and repeating navigation/deletion keys.
The common `:focusable` option overrides the default. Accepted repeats follow
the normal keymap and event routing path. Activation arms the pressed state
on keydown and sends `:press` once on matching keyup. A changed activation
policy cancels an armed key. Methods can be redefined without replacing the
mounted widget. The host follows `accepts-text?` when enabling SDL text input.

Run the calculator with either runtime:

```sh
./otium --runtime=chez examples/ui.ot -- \
  --scene ui.calculator --title Calculator --height 600
```

It uses rows, columns, labels and composed buttons, with a `defonce` model.
Digits, decimal point and operators work through a container keymap; Escape
clears. Arithmetic is immediate, left to right, with division-by-zero recovery.
The disabled Memory button demonstrates disabled styling. The calculator tests
cover arithmetic, native SDL key routing, keyboard activation, focus/pressed
pixels and identical C/Chez captures.

## Text fields and terminal

```sh
./otium --runtime=chez examples/ui.ot -- \
  --scene ui.terminal --title Terminal
```

The terminal runs `help`, `echo`, `history`, and `clear`. It does not launch
processes. Up/Down traverse command history and restore the draft when returning
to the newest position. Scrollback follows new output until the user scrolls up;
scrolling back to the bottom resumes following. Its model uses `defonce`, and
the prompt's draft and history position are mounted widget state.

`ui/text-field` accepts `:value`, `:on-change`, `:on-submit`, `:placeholder`,
`:font`, `:size`, `:width`, `:height`, and `:grow`. Keep the value in application
or parent-widget state and update it in `:on-change`. The field updates its
mounted value immediately as it edits, then calls `:on-change` with the new
string. `:on-submit` receives the value on Enter. A changed external value
moves the cursor to its end; unchanged values preserve cursor and selection.
Common `:autofocus #t` requests focus once when a widget mounts, if the tree
has no focused widget. Disabled fields neither focus nor accept input.

Left/Right and Home/End move the cursor; Shift extends selection. Backspace and
Delete remove a selection or one Unicode codepoint. Ctrl/Meta+A selects all;
Ctrl/Meta+C, X and V copy, cut and paste. Paste normalizes line breaks and tabs
to spaces. Editing keys repeat; Enter submits once. Pointer press places the
cursor and dragging selects. Long input scrolls horizontally to keep the cursor
visible. Cursor and selection positions are codepoint indices on both runtimes.
Caret coordinates come from the complete shaped line, including kerning.
Positions inside a ligature are interpolated across its shaped cluster; they
do not use font-provided ligature caret data. Measurement uses one shaped run
and copies all positions through one native call.
Grapheme-cluster navigation, bidirectional caret placement, word navigation,
undo/redo, multiline fields and IME composition are not implemented yet. The
caret is steady rather than blinking, including in deterministic captures.
`TextField` accepts `:caret-style :block` for a filled cursor; the default is
`:bar`. The terminal uses the block style, with contrasting text beneath the
cursor and enough horizontal scroll space to keep the full block visible.
Unwrapped text preserves trailing whitespace advances for caret movement,
selection, and pointer positioning.

Committed text arrives through SDL text events, separately from key events.
The host starts/stops SDL text input as focus changes. Headless tests send
`{:kind :text-input :text "café"}` to `input/keyboard!`. Clipboard key events
can supply `:clipboard-read` and `:clipboard-write` functions; the CLI supplies
SDL adapters, while tests use an in-memory clipboard.

The CLI reconciles after each input event so Enter followed immediately by
text starts a fresh prompt. It still lays out and paints once per frame.
No watcher or editor connection is involved.

`ui/scroll` takes children and `:width`, `:height`, `:grow`, `:padding`, `:gap`,
`:background`, and `:stick-to-bottom`. Give it a height or `:grow 1` in a bounded
column. Children retain their content height. The container clips painting and
hit testing, translates child bounds by its vertical offset, and shows a scroll
indicator. `:wheel` events carry logical pointer coordinates and `:dy`; native
wheel direction is normalized. `ui/scroll-to!` takes a mounted Scroll instance
and a logical pixel offset, clamping to its current range. Nested scroll views
pass wheel events outward when the inner view cannot move.

`:axis :x` lays children out in a row and scrolls horizontally. Wheel events
carry `:dx` as well as `:dy`; a horizontal view uses `:dx`, or `:dy` when `:dx`
is zero so a plain mouse wheel works. `:reveal` names a child key: after layout
the view scrolls just far enough to show that child. It does this again only
when the key, the child's position or size, or the viewport changes, so wheel
scrolling is not undone. Scrollbar dragging and virtualized line storage are
deferred.

A changed `:reveal` animates. The offset eases toward its target with an
exponential decay whose time constant is `:smoothing` seconds (default 0.06,
so most of the distance is covered in about 0.2 s); `:smoothing 0` jumps. The
first reveal and viewport changes jump. Wheel input and `ui/scroll-to!` move
immediately and end any animation; `ui/scroll-toward!` animates. Offsets are
snapped to physical pixels so text stays sharp while moving.

Frames that only advance animations skip reconciliation and Yoga:
`gpu/animate!` moves the scrolled widgets' bounds directly. While a visible
window animates, the loop does not sleep between frames and is paced by the
swapchain's vsync.

### Paint caches

Painting a large widget, such as a terminal, every frame is expensive.
`(gfx/cached g cache bounds key paint)` records what `paint` draws and replays
it on later frames, moved to the bounds' position and clipped by the current
ancestors. `paint` runs again only when `key`, the bounds' size or the pixel
scale changes. Keep one `(gfx/cache)` per widget, for example in a state slot.

The common `:cache #t` option does the same for a widget and all of its
descendants, keyed by the tree's paint generation. Layout and
`gpu/invalidate-paint!` start a new generation, so these subtrees repaint on
every full frame and replay on animation-only frames. A hover change during an
animation also starts a new generation.

Recordings live in the renderer. One not drawn for 120 frames is freed, so
removed or off-screen widgets need no cleanup. Replay skips instances wholly
outside the clip. In the Manse mock, scrolling the strip costs about 0.3 ms
of CPU per frame on Chez with caching, against about 4 ms without.

The checked-in editor and terminal scripts cover native Unicode input,
submit-then-type in one frame, history, wheel scrolling, draft restoration,
selection, horizontal input clipping, C/Chez pixel parity and resource teardown.
The layout suite also checks that long scrollback does not shrink the heading
or input field. Empty shaped-text cache hits have a regression check.

### Window resizing

The demo window is resizable. `otium.sdl` exposes `WINDOW_RESIZABLE`,
`set-window-size`, `get-window-size`, `get-window-size-in-pixels`, and
`get-window-pixel-density`. The size queries take native integer output pointers.

`native/poll-event` returns `{:kind :resize :width w :height h}` for window
size or pixel-size changes. Dimensions are in logical window coordinates;
nonpositive sizes are ignored. SDL distinguishes these from drawable pixels
([SDL high-DPI guide](https://wiki.libsdl.org/SDL3/README-highdpi)).
Call `(gpu/resize! tree renderer w h scale)` before routing further pointer
input. It updates renderer dimensions, lays out the retained tree, and returns
the effective scale. Widget state and focus survive resizing.
Scale 0 follows the window's current pixel density. Direct rendering requires
scale 0; offscreen rendering also accepts an explicit scale. The demo uses
scale 1 by default in headless mode.

### Direct and offscreen rendering

`(native/gpu-new window width height scale driver)` draws directly into SDL's
swapchain. SDL owns and resizes these images. Each frame acquires the current
image and renders into it, with no intermediate target or full-window copy.
If SDL returns no image while the window is hidden or minimized, drawing is
skipped until a surface becomes available again.

Pass `#t` as the optional sixth argument to select offscreen rendering at
initialization. `native/gpu-offscreen?` reports that choice. This mode uses an
RGBA texture for rendering and blits it to the window when a surface is
available. It supports captures and explicit render scaling. The demo selects
it for `--headless`, `--capture`, `--scale` greater than zero, or `--offscreen`.
The pipeline's pixel format is chosen for the selected mode at initialization.

Direct captures render one frame into a temporary texture with the swapchain's
pixel format, then blit it to the window. Both rendering modes allocate a
temporary readback buffer for captures, wait for that frame to finish, save
the image, and release the temporary storage. Ordinary frames allocate no
readback storage and submit asynchronously after acquiring the swapchain image.
See [SDL's swapchain contract](https://wiki.libsdl.org/SDL3/SDL_WaitAndAcquireGPUSwapchainTexture).

There is no application-specific limit on target dimensions. SDL and the GPU
determine whether an offscreen texture can be allocated; integer representation
checks prevent overflow before calling SDL. Captures additionally must fit SDL's
32-bit transfer-buffer size and signed surface dimensions and pitch.

`native/gpu-resize` returns false on invalid dimensions or allocation failure,
leaving the old dimensions and target usable. `gpu/resize!` raises that error;
applications that want to recover from allocation failure can use the native
result before changing their layout. Successful offscreen resizes release the
old target through SDL, which defers destruction until pending frames finish.
The pipeline and glyph atlas survive resizing in both modes.

## Live windows and development requests

`ui/open`, `ui/set-view!`, and `ui/run` now own the window lifecycle and event
loop used by the demo. `ui/windows` exposes handles and retained trees for
inspection; `ui/tree-data` reports layout and interaction state. `ui/capture!`
saves the next successful frame without requiring an offscreen window.

Run the demo with `--dev` and connect with `./otium attach`. Frame failures keep
the window open so a view can be fixed through evaluation or reload. See
[Development server](dev-server.md) for commands, watchdog behavior, and MCP use.
