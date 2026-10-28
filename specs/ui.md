# UI library

Status: draft, October 17, 2026. Widget lifecycle, constraint-aware Yoga layout,
wrapped text, rounded rectangles, pointer/keyboard button activation, focus
traversal and keymap actions work, including a widget calculator.
Single-line text editing, vertical scrolling and a command-history terminal
also work. Text editing currently uses codepoint boundaries
and shaped left-to-right caret placement, with interpolated positions inside
ligatures; IME composition remains deferred. Custom widget methods control
default focusability, committed text, key repeat and keyup activation.
The window/reload service works, including development
sockets and recovery from frame errors. See `examples/ui.ot` for a runnable
demo. Decisions
made so far are listed at the end.

`otium.ui` is a hardware-accelerated UI library written in Otium on top of the
portable FFI (`specs/ffi.md`) and classes (`specs/classes.md`). It has to handle three programs:

- **Boids**, a flocking simulation. Exercises the canvas and drawing throughput.
- **A calculator.** Layout, buttons, keyboard input, focus.
- **A toy terminal**: scrollback and an input line with history, not a real
  terminal emulator. Text input, scrolling, local state.

Full versions of all three, plus a custom widget, are at the end.

## Layers

| Layer | What it is |
| --- | --- |
| `otium.sdl` | Plain FFI bindings: windows, GPU device, events, text input, clipboard. Events are polled, so FFI v1's lack of callbacks doesn't matter. |
| Renderer | A small native library in `native/ui/` that draws a few primitive types on the GPU (see Rendering). Bound through the FFI. |
| Text | SDL_ttf 3 for shaping, measuring and turning glyphs into bitmaps; our own glyph atlas (see Text). |
| Layout | [Yoga](https://www.yogalayout.dev/) (flexbox), built in `native/` and bound through its C API (see Layout). |
| `otium.gfx` | Drawing calls for canvases and widget `paint` methods. |
| `otium.ui` | Windows, the event loop, widgets, focus, key bindings. |

Native code is only used where Otium can't do the job (GPU calls, font
rasterising) or where per-call cost matters (recording primitives).

## Update model

An application is a `view` function that returns a description of the UI.
After every event the library calls `view` again, from the root, and matches
the result against what's on screen. Events arriving within one frame are
handled first and `view` runs once.

The current CLI host reconciles between input events so a submit followed by
new text in the same frame observes the cleared application value. Layout and
painting still run once per frame. Batching view evaluation needs to preserve
this behavior before it replaces that implementation.

Matching happens among children of the same mounted widget. A keyed child
matches by widget class identity and `:key`; an unkeyed child matches by class
identity and sibling position. Keys must be unique among siblings. Reordering
keyed children preserves their instances; moving a child to another parent
creates a new instance.

Each constructor produces a fresh description. The library keeps a separate,
persistent mounted instance of the same class. On a match it copies the
caller-supplied properties into that instance and preserves its state slots,
focus, text cursor and selection, scroll position, and valid layout and paint
caches. Properties omitted from the new description take their defaults; they
don't retain an old caller-supplied value.

After updating an instance, the library calls its `build` method and matches
the returned children recursively. Unmatched descriptions get new mounted
instances. Instances with no match are removed along with their state and
native resources. Methods and event handlers operate on mounted instances.

Reload goes through the same path. Re-evaluating a file defines new functions
and methods, then the library re-runs `view` and matches as usual. Class
redefinition preserves class identity as in `specs/classes.md`. State slots
with the same names keep their values; added state slots get their initial
values and removed slots are dropped. Renaming a state slot resets it.
Changing a slot between property and state also resets that slot. There is no
separate UI rebuild on reload. Initially, reload invalidates layout and paint
caches conservatively, including when only a method changed.

The model (application data) lives in `defonce` and is changed by ordinary
code in event handlers, mutating in place if that's convenient. Nothing tracks
model reads: re-running from the root after every event observes the current
values. Paint and layout caches must still account for mutable inputs.

When nothing happens, no view, layout or paint work runs. A canvas that
animates is the exception: its tick runs before view evaluation each frame,
so changes it makes to the model can also update other widgets.

Re-running everything on every event costs time proportional to the size of
the UI. Tracking which widgets read what, so only those re-run, is deferred.
It can be added later without changing application code.

### Reusing work

Descriptions and mounted instances use fixed class layouts, with cached slot
access and generic dispatch. Persistent widget state lives in slots on the
mounted instance. The library holds layout nodes, focus handles and native
recordings alongside it.

Reusing layout or paint requires unchanged relevant properties, state,
children and rendering context, including bounds, inherited styles and display
scale. `eq?` alone is insufficient: a mutable array can change without changing
identity. Initially, widgets with mutable inputs are conservatively invalidated
after view evaluation. Specialized widgets can keep snapshots or revisions to
avoid unnecessary work. A changed event handler must be installed even when
layout and paint can be reused. Custom widgets default to conservative
invalidation until their dependencies can be established.

A subtree whose paint is reused costs one call to replay its recorded
primitives. Recordings must retain valid image and glyph references or be
invalidated when those resources change.

## Widgets and state

Widgets are class instances. Every widget class has `ui/Widget` as an
ancestor. A widget can return children, draw into its bounds, or do both.
The library calls five generics:

```scheme
(defgeneric (ui/build widget))                ; return child descriptions
(defgeneric (ui/layout-style widget))         ; LayoutStyle record or custom style table
(defgeneric (ui/measure widget limits))       ; preferred size within limits
(defgeneric (ui/paint widget g bounds))       ; draw into bounds
(defgeneric (ui/handle-event widget event))   ; return :handled to stop it
```

These operations dispatch on the widget class only. Limits, drawing contexts,
bounds and events are ordinary arguments; UI methods do not specialize on
those arguments. This uses the existing generic machinery without requiring
multiple dispatch in the UI API. The language's multimethod support is
unchanged.

A new widget is a class plus methods. Nothing central needs editing. The
default `build` returns constructor-supplied children, or no children. The
default `paint` draws nothing. A widget using both paints before its children;
the library lays out, paints and dispatches events to those children normally.
A widget that only builds children uses the default child-based measurement
and layout; a directly drawn leaf supplies its own measurement.

Slots are caller-supplied properties unless marked `:state #t`:

```scheme
(defclass Dropdown :is ui/Widget
  items
  on-pick
  (open? :init #f :state #t))

(defmethod (ui/build (d Dropdown))
  (ui/column {}
    (ui/button
      {:on-press (fn () (set! (d :open?) (not (d :open?))))}
      "▾")
    (when (d :open?)
      (map
        (fn (item)
          (ui/button
            {:key item
             :on-press (fn () ((d :on-pick) item)
                             (set! (d :open?) #f))}
            item))
        (d :items)))))

(defn dropdown (opts) (ui/new Dropdown opts))
```

Here the item strings must be unique because they are used as sibling keys.
`items` and `on-pick` are replaced from each new description. `open?` is
initialized when the widget mounts and survives matching and reload. The
handler captures the mounted instance `d`; ordinary slot reads see its current
properties and state when the handler runs.

`:state` is slot metadata described in `specs/classes.md`. State slots require an initializer. UI description construction
validates and initializes properties without evaluating state initializers;
mounting evaluates those initializers once per instance. State slots cannot
be supplied through widget constructor options. This requires a class-aware
description construction path, rather than calling ordinary `make` unchanged.
Methods still use ordinary cached slot reads and writes.

Use state slots for things that only matter to one widget (open menus, an
unsubmitted draft). Application data belongs in the model. Helpers that just
return widgets are ordinary functions; a helper needing its own persistent
state becomes a widget class. Repeated stateful widgets need a `:key` per item.
`(ui/inspect widget)` shows the properties, state slots and library-held state
of a mounted instance.

There is no separate component abstraction, `defcomponent`, `let-state` or
`defwidget` syntax.

The library clips `paint` to the widget's bounds, skips `handle-event` for
disabled widgets, and draws debug outlines. Widgets can also share behaviour
by holding helper objects in slots and forwarding to them, as in
`specs/classes.md`.

### Options and children

Every widget constructor takes an options table first, then children:

```scheme
(ui/column {:padding 12 :gap 8}
  (ui/label {:size 18} title)
  (when error (ui/label {:color 0xd04040ff} error))
  (map row-widget rows))
```

- Options populate declared property slots, so a misspelled option or a state
  slot supplied as an option is an error. `(ui/new Class opts child ...)`
  constructs a description for a custom widget; `ui/build` is the method the
  library calls on its mounted instance.
- `:key` is taken out before that and kept by the library, so widget classes
  don't declare it.
- A child is a widget, a list or array of children (flattened at any depth),
  `nil` (skipped), or a string (becomes a label).

### Built-in widgets

Containers: `row`, `column`, `grid`, `scroll`. Leaves: `label`, `button`,
`slider`, `text-field`, `lines` (many lines of text as one widget, drawing
only the visible ones), `canvas`.

A `canvas` takes `:paint`, called with `g` and its bounds whenever it needs
drawing, and optionally `:tick`, called once per frame with the elapsed time.
Having a `:tick` means it animates.

## Input, actions and focus

Events have a `:kind` slot, such as `:pointer-drag`, and payload slots for that
kind. A widget's `handle-event` method checks the kind and returns `:handled`
when it consumes the event, or `nil` otherwise. For example, pointer drag
events provide `:dx` and `:dy` in logical pixels. Widgets do not need a separate
method specialization for each event kind.

- **Key bindings map to named actions.** A `:keymap` option maps keys to
  actions, and `:on-action` handles them. Handlers respond to `:older`, not to
  "up arrow", so bindings are data that can be edited and reloaded. Lookup
  starts at the focused widget and walks up to the window.
- **Focus is an object.** The library owns a focus handle per focusable widget,
  kept in its widget state. Widgets opt in with `:focusable #t`; text fields
  and buttons are focusable by default. Tab moves focus; Enter and Space
  activate the focused button.
- Text input uses SDL's text input events, separate from key events. IME
  composition is deferred.
- Pointer press captures the pointer; release decides activation.

## Windows and the event loop

```scheme
(defonce window (ui/open {:title "Boids" :width 1200 :height 640}))
(ui/set-view! window view)   ; runs again on reload, installing the new view
(ui/run)                     ; blocks the first time; does nothing if running
```

- `defonce` means reloading never opens a second window.
- Multiple windows are one `defonce` and one `set-view!` each.
- `ui/run` blocks until the last window closes. Between frames it services
  reload requests, from a file watcher or the editor connection, the way the
  current SDL host does. That is how re-evaluating a file reaches
  `set-view!` while the loop runs.
- The loop waits for events with a timeout so the VM's interrupt checks still
  run when idle.
- While `ui/run` blocks, the REPL prompt in that process is not available.
  Evaluation from the editor goes through the loop. A general task system is
  out of scope.

## Rendering

Following gpui, everything is drawn with a few primitive types:

- rounded rectangles, with fill and border,
- shadows,
- glyphs, from an atlas texture,
- paths, for lines, arcs and canvas shapes,
- images.

Primitives are recorded into native memory, grouped by type and layer, and
drawn with a handful of instanced draw calls per frame. Edges are smoothed in
the shaders, so rounded corners and circles look right without multisampling.

This means using SDL_GPU with a few shaders rather than SDL's 2D renderer,
which has no smoothing and would need shapes broken into triangles on the CPU.

A widget's paint output is recorded per subtree. An unchanged subtree replays
its recording with one call.

`otium.gfx` is the same API for widget `paint` methods and canvases:
`gfx/clear`, `gfx/rect`, `gfx/circle`, `gfx/arc`, `gfx/line`, `gfx/triangle`,
`gfx/path`, `gfx/text`, `gfx/image`, plus clipping and transforms. Colours are
`0xRRGGBBAA` integers.

## Text

SDL_ttf 3 does shaping (it bundles HarfBuzz), measuring, fallback fonts
(`TTF_AddFallbackFont`) and turning glyphs into bitmaps (it bundles FreeType).
It is the same family as the SDL3 dependency and builds for the web.

Only those parts are used. A custom `TTF_TextEngine` reads SDL_ttf's shaped glyph
operations without drawing through its built-in renderers. Glyph bitmaps go into
an atlas texture the renderer owns, and glyphs are one of its primitive types.

A default UI font and a monospace font are bundled, both under the SIL Open
Font License (Noto Sans and JetBrains Mono in the current implementation). Text looks the same on
every machine, pixel tests are deterministic, and the terminal doesn't depend
on installed fonts.

Smoothing is grayscale anti-aliasing, not subpixel (LCD).

Not now, and none of them would change the atlas or renderer:

- Platform text systems (Core Text, DirectWrite). They mean an implementation
  per platform and rendering that differs by machine. Linux has no single
  equivalent; its desktop stack is fontconfig, FreeType and HarfBuzz.
- fontconfig, to fall back to fonts the user has installed.
- stb_truetype for very constrained targets, accepting no shaping.
- Signed-distance-field glyphs, if zooming text smoothly becomes important.

## Layout

Layout is Yoga, bound rather than ported. Yoga is well tested against Chrome,
and the layout logic itself doesn't need to be live-reloadable: everything an
application edits (widgets, options, `measure` and `paint` methods) is still
Otium and reloads as usual.

- Yoga is C++20. It's compiled only in `native/`, which keeps the runtime
  itself C-only.
- Yoga asks leaf nodes for their size through a measure callback. FFI v1 has
  no interpreter callbacks. The native callback records constraint queries;
  Otium answers them through `ui/measure` between Yoga passes. Replay continues
  until all requests have answers. Node contexts contain only native data,
  so no managed pointers cross the bridge.
- The library keeps one Yoga node per matched widget, creating and freeing
  nodes as matching adds and removes widgets, and sets styles on the nodes it
  keeps.
- Yoga has no grid layout, so `grid` is our own container.

Layout uses floats in logical pixels. Rectangle edges are snapped to physical
pixels when painting, so neighbouring widgets meet exactly. Canvases and
animated transforms aren't snapped. Yoga can do the snapping itself if given
the display scale.

A port of a flexbox subset to Otium was estimated at 700-1,000 lines, with
about 185 of Yoga's generated tests applying to it. That stays an option if the
C++ dependency becomes a problem, with the bound Yoga as a test oracle.

## Performance

GPU work is cheap. The cost is Otium code running per widget per event, so
the plan is to make that fast rather than design around it:

- a dispatch cache per generic (`specs/classes.md`),
- cached slot access, `(b :x)` as a layout check plus an index,
- unboxed floats,
- cheap allocation and collection of short-lived descriptions,
- the per-subtree paint cache and reuse of unchanged subtrees,
- native recording of primitives, one FFI call per primitive.

Targets, to be measured once something runs: no work when idle; a keystroke in
the terminal well under a millisecond of Otium work; boids with 600 birds at
60 fps.

## Testing

- Headless tests for matching, keys, property replacement, state initialization
  and preservation, state-slot migration on reload, children flattening, action
  lookup and focus movement, with fake measurement and paint, including handlers retained across
  property updates.
- Display tests in the native suite: scripted input, frame capture, and
  pixel checks, as the current SDL tests do.
- Every example supports bounded runs, scripted input, a fixed timestep and
  random seed, and screenshots written before clean exit. Capture waits for
  GPU readback. State dumps include widget identities, bounds, focus and state
  slots so a program can be inspected without interactive use. Tests synchronize
  reloads through status acknowledgements rather than sleeps. Failed runs keep
  screenshots and diagnostics and return a nonzero exit status.
- The four example programs below.

## Deferred

- Change tracking so only affected widgets re-run, and persistent
  collections to make changes cheap to detect.
- IME composition, accessibility, drag and drop.
- Async work and a task system.
- Damage-region redraw.

## Open questions

1. Does `canvas` `:tick` pass a time delta, a frame count, or both?
2. How does reload find the files to re-evaluate: a watcher in the loop,
   editor-driven only, or both?

## Decisions so far

- Re-run from the root after every event and on reload, with matching by type,
  key and position. No change tracking for now.
- One class-based widget model, with `build`, `measure`, `paint` and
  `handle-event` methods.
- Fresh descriptions update properties on persistent mounted instances;
  declared `:state` slots survive matching and reload.
- Fixed slot layouts and cached access and dispatch for properties and state.
- UI methods dispatch on the widget class only; event kinds are data.
- Options table first, then children; one flattening rule for children.
- `ui/open`, `ui/set-view!` and `ui/run` are separate.
- A few GPU primitive types with shader smoothing, on SDL_GPU.
- Key bindings map to named actions; focus is an object.
- Layout uses floats, snapped to pixels at paint.
- Layout is Yoga bound through the FFI, with one native measure callback.
- Text through SDL_ttf 3, with our own glyph atlas and bundled fonts.

## Examples

All four use the class syntax from `specs/classes.md`. These are target API
examples, not yet runnable applications. The implemented subset has widget
construction, matching, state, rows and columns, constraint-aware layout, wrapped
labels, rounded rectangles, pointer-driven buttons, focus, and the window
service. The remaining drawing primitives below are still proposed. See
`examples/ui.ot` for runnable code.

### Boids

```scheme
;; boids -- flocking birds on a canvas, with sliders to tune the rules.
;;
;; `flock` and `rules` are the model; `defonce` keeps them across reloads.
;; `tick!` moves the birds each frame and `paint` draws them. `view` describes
;; the window and runs again after every event and every reload.
(ns demo.boids)
(require (otium.ui :as ui))
(require (otium.gfx :as gfx))

;; One bird: position in pixels, velocity in pixels per frame.
(defclass Boid x y vx vy)

;; Flocking settings. The sliders write these slots directly.
(defclass Rules
  (sight :init 50.0)      ; how far a bird sees its neighbours
  (spacing :init 16.0)    ; closer than this is too close
  (align :init 0.05)      ; pull toward neighbours' average heading
  (cohere :init 0.004)    ; pull toward neighbours' centre
  (separate :init 0.06)   ; push away from birds that are too close
  (speed :init 3.0))      ; maximum speed

(defonce rules (make Rules))

(defn spawn (w h)
  (make Boid :x (random w) :y (random h)
             :vx (- (random 2.0) 1.0) :vy (- (random 2.0) 1.0)))

(defn scatter () (let ((a [])) (dotimes (i 600) (push! a (spawn 960 640))) a))
(defonce flock (scatter))

;; Birds leaving one edge come back on the opposite edge.
(defn wrap (v size)
  (cond ((< v 0) (+ v size)) ((>= v size) (- v size)) (else v)))

;; Adjust one bird's velocity using every bird it can see. Checking all pairs
;; is fine for a few hundred birds; a grid would be the next step.
(defn steer! (b)
  (with-slots b (x y vx vy)
    ;; n: neighbours seen; c: sum of offsets to them (points at their centre);
    ;; a: sum of their velocities; s: push away from crowding birds.
    (let ((n 0) (cx 0.0) (cy 0.0) (ax 0.0) (ay 0.0) (sx 0.0) (sy 0.0))
      (for-each
        (fn (o)
          (let ((dx (- (o :x) x)) (dy (- (o :y) y))
                (d (sqrt (+ (* dx dx) (* dy dy)))))
            ;; d is 0 only for this bird itself.
            (when (and (pos? d) (< d (rules :sight)))
              (set! n (inc n))
              (set! cx (+ cx dx)) (set! cy (+ cy dy))
              (set! ax (+ ax (o :vx))) (set! ay (+ ay (o :vy)))
              (when (< d (rules :spacing))
                (set! sx (- sx (/ dx d))) (set! sy (- sy (/ dy d)))))))
        flock)
      ;; Mix the three pulls into the velocity, each scaled by its rule.
      (when (pos? n)
        (set! vx (+ vx (* (rules :cohere) cx)
                       (* (rules :align) (- (/ ax n) vx))
                       (* (rules :separate) sx)))
        (set! vy (+ vy (* (rules :cohere) cy)
                       (* (rules :align) (- (/ ay n) vy))
                       (* (rules :separate) sy)))))))

;; Cap the speed, then take one step and wrap around the edges.
(defn move! (b w h)
  (with-slots b (x y vx vy)
    (let ((s (sqrt (+ (* vx vx) (* vy vy))))
          (k (if (> s (rules :speed)) (/ (rules :speed) s) 1.0)))
      (set! vx (* vx k))
      (set! vy (* vy k))
      (set! x (wrap (+ x vx) w))
      (set! y (wrap (+ y vy) h)))))

;; Called once per frame. All birds steer before any move, so every bird
;; sees the same frame.
(defn tick! (dt bounds)
  (for-each steer! flock)
  (for-each (fn (b) (move! b (ui/width bounds) (ui/height bounds))) flock))

;; Each bird is a small triangle pointing the way it's going.
(defn paint (g bounds)
  (gfx/clear g 0x0c1020ff)
  (for-each
    (fn (b)
      (let ((x (b :x)) (y (b :y))
            (a (atan (b :vy) (b :vx))))
        (gfx/triangle g
          (+ x (* 7 (cos a))) (+ y (* 7 (sin a)))
          (+ x (* 4 (cos (+ a 2.5)))) (+ y (* 4 (sin (+ a 2.5))))
          (+ x (* 4 (cos (- a 2.5)))) (+ y (* 4 (sin (- a 2.5))))
          0x7fb2ffff)))
    flock))

;; One labelled slider bound to one slot of `rules`. The :key keeps it matched
;; to the same on-screen slider each run, so a drag in progress survives.
(defn rule-slider (label slot lo hi)
  (ui/row {:gap 8}
    (ui/label {:width 72} label)
    (ui/slider {:key slot :grow 1 :min lo :max hi
                :value (rules slot)
                :on-change (fn (v) (set! (rules slot) v))})))

(defn view ()
  (ui/row {}
    (ui/canvas {:key :flock :grow 1 :tick tick! :paint paint})
    (ui/column {:width 240 :padding 12 :gap 6}
      (ui/label {:size 18} "Rules")
      (rule-slider "Sight"    :sight    10 120)
      (rule-slider "Spacing"  :spacing  4 40)
      (rule-slider "Align"    :align    0 0.2)
      (rule-slider "Cohere"   :cohere   0 0.02)
      (rule-slider "Separate" :separate 0 0.2)
      (rule-slider "Speed"    :speed    0.5 8)
      (ui/button {:on-press (fn () (set! flock (scatter)))} "Scatter"))))

(defonce window (ui/open {:title "Boids" :width 1200 :height 640}))
(ui/set-view! window view)
(ui/run)
```

### Calculator

```scheme
;; calc -- four-function calculator, by mouse or keyboard.
;;
;; Buttons and keys both produce actions: a digit string like "7", or a
;; keyword like :add. `press!` handles every action.
(ns demo.calc)
(require (otium.ui :as ui))

(defclass Calc
  (entry :init "0")     ; the text in the display
  (acc :init nil)       ; running result, nil if none yet
  (op :init nil)        ; pending operation, :add :sub :mul or :div
  (fresh? :init #t))    ; the next digit starts a new number

(defonce calc (make Calc))

;; Button layout, row by row. Strings are digits; keywords are operations.
(def pad [["7" "8" "9" :div] ["4" "5" "6" :mul]
          ["1" "2" "3" :sub] ["0" "." :eq  :add]])

(def labels {:div "÷" :mul "×" :sub "−" :add "+" :eq "="})

;; Keyboard key to action. Digit keys map to themselves.
(def keymap
  (let ((m {"+" :add "-" :sub "*" :mul "/" :div "=" :eq
            "enter" :eq "escape" :clear "backspace" :back}))
    (for-each (fn (d) (put! m d d)) ["0" "1" "2" "3" "4" "5" "6" "7" "8" "9" "."])
    m))

;; Dividing by zero gives nil, shown as "Error".
(defn apply-op (op a b)
  (cond ((eq? op :add) (+ a b))
        ((eq? op :sub) (- a b))
        ((eq? op :mul) (* a b))
        ((eq? op :div) (if (zero? b) nil (/ a b)))))

(defn show! (v) (set! (calc :entry) (if (nil? v) "Error" (number->string v))))

(defn digit! (d)
  (let ((e (calc :entry)))
    (set! (calc :entry)
      (cond ((or (calc :fresh?) (equal? e "0")) (if (equal? d ".") "0." d))
            ((and (equal? d ".") (string-contains? e ".")) e)   ; one point only
            (else (str e d))))
    (set! (calc :fresh?) #f)))

;; Fold the pending operation into the running result.
(defn settle! ()
  (let ((n (string->number (calc :entry))))
    (set! (calc :acc) (if (calc :op) (apply-op (calc :op) (calc :acc) n) n))))

;; An operation key, or = when op is nil. Pressing + twice shouldn't compute
;; twice, so only settle if a number was typed since the last operation.
(defn op! (op)
  (when (or (not (calc :fresh?)) (nil? (calc :acc)))
    (settle!))
  (set! (calc :op) op)
  (set! (calc :fresh?) #t)
  (show! (calc :acc)))

(defn press! (k)
  (cond ((string? k) (digit! k))
        ((eq? k :clear) (set! calc (make Calc)))
        ((eq? k :back)
         (let ((e (calc :entry)))
           (set! (calc :entry)
             (if (<= (string-length e) 1) "0"
                 (substring e 0 (dec (string-length e)))))))
        ((eq? k :eq) (op! nil))
        (else (op! k))))

(defn key-button (k)
  (ui/button {:key k :grow 1
              :style (if (string? k) :plain :accent)
              :on-press (fn () (press! k))}
    (if (string? k) k (get labels k))))

;; :keymap and :on-action on the outer column catch keys anywhere in the window.
(defn view ()
  (ui/column {:padding 12 :gap 8 :keymap keymap :on-action press!}
    (ui/label {:align :right :size 40 :font :mono} (calc :entry))
    (map (fn (row) (ui/row {:gap 8 :grow 1} (map key-button row))) pad)))

(defonce window (ui/open {:title "Calculator" :width 320 :height 420}))
(ui/set-view! window view)
(ui/run)
```

### Terminal

```scheme
;; term -- a toy shell: scrollback, an input line, up/down history.
;; It runs a few built-in commands, not real programs.
(ns demo.term)
(require (otium.ui :as ui))

(defclass Term
  (lines :init ["otium toy shell -- try: help"])   ; everything shown
  (history :init []))                              ; commands, oldest first

(defonce term (make Term))

;; Mutating in place is fine: view re-runs after every event.
(defn emit! (line) (push! (term :lines) line))

;; Name to function from argument words to an array of output lines.
(def commands
  {"help"    (fn (args) ["help  echo  clear  history  eval <expr>"])
   "echo"    (fn (args) [(string-join args " ")])
   "clear"   (fn (args) (set! (term :lines) []) [])
   "history" (fn (args) (copy (term :history)))
   "eval"    (fn (args) [(str (eval (read-string (string-join args " "))))])})

(defn run! (input)
  (let ((words (filter (fn (w) (not (empty? w))) (string-split input " ")))
        (cmd (get commands (first words))))
    (emit! (str "$ " input))
    (unless (empty? words) (push! (term :history) input))
    (cond ((empty? words) nil)
          ;; An error inside a command becomes a line of output.
          (cmd (for-each emit!
                 (try (cmd (rest words))
                   (catch (error? e) [(str "error: " (condition-message e))]))))
          (else (emit! (str (first words) ": command not found"))))))

;; The input line owns its draft and history position.
;; back = 1 is the newest history entry; 0 returns to an empty draft.
(defclass Prompt :is ui/Widget
  (draft :init "" :state #t)
  (back :init 0 :state #t))

(defmethod (ui/build (p Prompt))
  (ui/text-field
    {:grow 1 :autofocus #t
     :value (p :draft)
     :on-change (fn (s) (set! (p :draft) s))
     :on-submit (fn (s) (run! s)
                       (set! (p :draft) "") (set! (p :back) 0))
     :keymap {"up" :older "down" :newer}
     :on-action
     (fn (a)
       (let ((h (term :history)))
         (set! (p :back) (if (eq? a :older)
                            (min (length h) (inc (p :back)))
                            (max 0 (dec (p :back)))))
         (set! (p :draft)
           (if (zero? (p :back)) ""
               (get h (- (length h) (p :back)))))))}))

;; :stick-to-bottom keeps the scrollback at the end as lines arrive, unless
;; the user has scrolled up.
(defn view ()
  (ui/column {:font :mono :background 0x101418ff}
    (ui/scroll {:key :scrollback :grow 1 :stick-to-bottom #t :padding 8}
      (ui/lines {:color 0xc8d3e0ff} (term :lines)))
    (ui/row {:padding 8 :gap 8}
      (ui/label {:color 0x7fb2ffff} "$")
      (ui/new Prompt {:key :prompt}))))

(defonce window (ui/open {:title "Terminal" :width 720 :height 480}))
(ui/set-view! window view)
(ui/run)
```

### A custom widget

```scheme
;; knob -- a rotary knob, built from methods rather than other widgets.
;; Use it like a built-in:
;;   (knob {:key :volume :value v :min 0 :max 11 :on-change set-volume!})
(ns demo.knob)
(require (otium.ui :as ui))
(require (otium.gfx :as gfx))

(defclass Knob :is ui/Widget
  value
  (min :init 0.0)
  (max :init 1.0)
  on-change)

;; Options become slots; :key is kept by the library.
(defn knob (opts) (ui/new Knob opts))

(defmethod (ui/measure (k Knob) limits) (ui/size 48 48))

;; A dark disc, a blue arc showing the value, and a pointer.
(defmethod (ui/paint (k Knob) g bounds)
  (let ((t (/ (- (k :value) (k :min)) (- (k :max) (k :min))))   ; 0..1
        ;; Radians: start bottom-left (about 135°) and sweep 270°.
        (a (+ 2.36 (* t 4.71)))
        (cx (ui/center-x bounds)) (cy (ui/center-y bounds)))
    (gfx/circle g cx cy 22 0x1c2430ff)
    (gfx/arc g cx cy 20 2.36 a 3 0x7fb2ffff)
    (gfx/line g cx cy (+ cx (* 14 (cos a))) (+ cy (* 14 (sin a))) 2 0xe6edf5ff)))

;; Dragging up turns it up; 200 px covers the full range. The knob reports
;; the new value and the application decides what to do with it.
(defmethod (ui/handle-event (k Knob) e)
  (if (eq? (e :kind) :pointer-drag)
      (let ((v (- (k :value) (* (e :dy) (/ (- (k :max) (k :min)) 200)))))
        ((k :on-change) (max (k :min) (min (k :max) v)))
        :handled)
      nil))
```

### Temporary layout values and descriptions

Built-in layout methods return `ui/LayoutStyle` records. Custom methods may
still return tables. Measurement receives an immutable `ui/MeasureLimits`
record with width, height, width-mode, height-mode and scale fields. Use
`(ui/limit-ref limits :width fallback)` for unconstrained dimensions, whose
record fields are nil. Only leaves with a specialized measurement method are
measured; layout refreshes the method list each pass to follow live reloads.

Treat descriptions and their options as fixed once constructed. `ui/new`
accepts nil for empty options, shares nonempty framework options with the
caller, and stores nil when there are no framework options. Helpers such as
`row` and `label` do not mutate the caller's options.

Handled input requests a view update before the next event or frame. Hover
changes and expose events only repaint; unhandled input with unchanged hover
and focus does not request a frame.
