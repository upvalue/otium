# UI library

Status: draft, October 17, 2026. Not implemented. Replaces the September plan
in `c/agent-docs/ui-plan.md`, which predates the FFI, the common reader and
the class system. Decisions made so far are listed at the end.

`otium.ui` is a hardware-accelerated UI library written in Otium on top of the
portable FFI (`specs/ffi.md`) and classes (`specs/classes.md`). It is shared
source: the same library runs on C and Chez. It has to handle three programs:

- **Boids**, a flocking simulation. Exercises the canvas and drawing throughput.
- **A calculator.** Layout, buttons, keyboard input, focus.
- **A toy terminal**: scrollback and an input line with history, not a real
  terminal emulator. Text input, scrolling, local state.

Full versions of all three, plus a custom widget, are at the end.

## Layers

| Layer | What it is |
| --- | --- |
| `otium.sdl` | Plain FFI bindings: windows, GPU device, events, text input, clipboard. Events are polled, so FFI v1's lack of callbacks doesn't matter. |
| Renderer | A small native library in `shared/` that draws a few primitive types on the GPU (see Rendering). Bound through the FFI. |
| Text | SDL_ttf 3 for shaping, measuring and turning glyphs into bitmaps; our own glyph atlas (see Text). |
| Layout | [Yoga](https://www.yogalayout.dev/) (flexbox), built in `shared/` and bound through its C API (see Layout). |
| `otium.gfx` | Drawing calls for canvases and widget `paint` methods. |
| `otium.ui` | Windows, the event loop, components, widgets, focus, key bindings. |

Native code is only used where Otium can't do the job (GPU calls, font
rasterising) or where per-call cost matters (recording primitives).

## Update model

An application is a `view` function that returns a description of the UI.
After every event the library calls `view` again, from the root, and matches
the result against what's on screen. Events arriving within one frame are
handled first and `view` runs once.

Matching pairs each new widget with an existing one by widget type, then
`:key` if given, then position among its siblings. A matched widget keeps
everything the library holds for it:

- its layout and paint caches,
- focus, text cursor and selection, scroll position,
- local state declared with `let-state`.

Unmatched widgets are created, and widgets with no match in the new result
are removed along with their state.

Reload goes through the same path. Re-evaluating a file defines new functions
and methods, then the library re-runs `view` and matches as usual. Changing a
colour repaints one widget. Adding a button inserts one widget. State resets
only where the structure changed. There is no separate rebuild on reload.

The model (application data) lives in `defonce` and is changed by ordinary
code in event handlers, mutating in place if that's convenient. Nothing tracks
changes: re-running from the root after every event means nothing needs to.

When nothing happens, nothing runs. A canvas that animates is the exception:
it repaints every frame while animating.

Re-running everything on every event costs time proportional to the size of
the UI. That is acceptable given the performance work below. Tracking which
components read what, so only those re-run, is deferred. It can be added later
without changing application code.

### Reusing work

Each run allocates a fresh description. Persistent data for a widget lives in
a table the library keeps, keyed by the widget's path (types, keys and
positions from the root), not in the description.

If a widget's description has the same slot values as last time (compared
with `eq?`) and so do all its descendants, its cached layout and paint are
reused. A subtree whose paint is reused costs one call to replay its recorded
primitives.

## Components and local state

A component is a function returning a description. A `defcomponent` is one
that can have local state:

```scheme
(defcomponent (dropdown items on-pick)
  (let-state ((open? #f))
    (ui/column {}
      (ui/button {:on-press (fn () (set! open? (not open?)))} "▾")
      (when open? (ui/list {} items on-pick)))))
```

- Local state belongs to the component instance and is identified by the
  variable name. Conditionals and reordering are fine; there is no ordering
  rule as with React hooks. Renaming a variable resets it.
- `set!` on the name stores the new value, including from handlers that run
  later. Reading the name always gives the current value, so handlers never
  see a stale copy. This needs local symbol macros in the shared expander.
- A helper that needs its own state must itself be a `defcomponent`.
- State inside a loop needs one instance per item, so it has to be in a child
  component with a `:key`.
- `(ui/inspect widget)` shows a widget's local and library-held state from
  the REPL.

Use local state for things that only matter to one piece of UI (open menus,
an unsubmitted draft). Application data belongs in the model.

## Widgets

Widgets are class instances. Every widget class has `ui/Widget` as an
ancestor. The library calls three generics:

```scheme
(defgeneric (ui/measure widget limits))       ; preferred size within limits
(defgeneric (ui/paint widget g bounds))       ; draw into bounds
(defgeneric (ui/handle-event widget event))   ; return :handled to stop it
```

A new widget is a class plus methods. Nothing central needs editing.

The library does the wrapping that would otherwise be repeated in every
method: it clips `paint` to the widget's bounds, skips `handle-event` for
disabled widgets, and draws debug outlines. Widgets share behaviour by
composition, holding helper objects in slots and forwarding to them, as in
`specs/classes.md`.

### Options and children

Every widget constructor takes an options table first, then children:

```scheme
(ui/column {:padding 12 :gap 8}
  (ui/label {:size 18} title)
  (when error (ui/label {:color 0xd04040ff} error))
  (map row-widget rows))
```

- Options become slot values through `make`, so a misspelled option is an
  error. `(ui/build Class opts children)` does this for custom widgets.
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

Only those parts are used. SDL_ttf's own text engines draw for you, which would
keep glyphs out of the renderer's batches. Instead, glyph bitmaps go into an
atlas texture the renderer owns, and glyphs are one of its primitive types.

A default UI font and a monospace font are bundled, both under the SIL Open
Font License (for example Inter and JetBrains Mono). Text looks the same on
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

- Yoga is C++20. It's compiled only in `shared/`, which keeps the runtimes
  themselves C-only. The WASM build compiles it with emscripten.
- Yoga asks leaf nodes for their size through a measure callback. FFI v1 has
  no callbacks, so `shared/` provides one fixed-signature C function that
  forwards to `ui/measure`. On C the node's context holds a handle to the
  widget, not a raw pointer, since the collector moves objects.
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
- unboxed floats on C,
- cheap allocation and collection of short-lived descriptions,
- the per-subtree paint cache and reuse of unchanged subtrees,
- native recording of primitives, one FFI call per primitive.

Targets, to be measured once something runs: no work when idle; a keystroke in
the terminal well under a millisecond of Otium work; boids with 600 birds at
60 fps on both runtimes.

## Testing

- Headless tests for matching, keys, local state, children flattening, action
  lookup and focus movement, with a fake measure and paint.
- Display tests behind `make test-sdl`: scripted input, frame capture, and
  pixel checks, as the current SDL tests do.
- The four example programs below, run on both runtimes.

## Deferred

- Change tracking so only affected components re-run, and persistent
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
- `defcomponent` and `let-state`, with state identified by variable name.
- Widgets are class instances with `measure`, `paint` and `handle-event`
  methods; composition, not inheritance.
- Options table first, then children; one flattening rule for children.
- `ui/open`, `ui/set-view!` and `ui/run` are separate.
- A few GPU primitive types with shader smoothing, on SDL_GPU.
- Key bindings map to named actions; focus is an object.
- Layout uses floats, snapped to pixels at paint.
- Layout is Yoga bound through the FFI, with one native measure callback.
- Text through SDL_ttf 3, with our own glyph atlas and bundled fonts.

## Examples

All four use the class syntax from `specs/classes.md`. Everything under
`ui/` and `gfx/`, `defcomponent`, `let-state` and `random` is proposed, not
existing.

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

;; The input line. Its draft text and history position are local state:
;; they belong to this widget, not the model.
;;   draft  the text being typed
;;   back   steps back through history; 0 means showing the user's own text
(defcomponent (prompt)
  (let-state ((draft "") (back 0))
    (ui/text-field
      {:key :prompt :grow 1 :autofocus #t
       :value draft
       :on-change (fn (s) (set! draft s))
       :on-submit (fn (s) (run! s) (set! draft "") (set! back 0))
       :keymap {"up" :older "down" :newer}
       :on-action
       (fn (a)
         (let ((h (term :history)))
           (set! back (if (eq? a :older)
                          (min (length h) (inc back))
                          (max 0 (dec back))))
           ;; back = 1 is the newest entry.
           (set! draft (if (zero? back) "" (get h (- (length h) back))))))})))

;; :stick-to-bottom keeps the scrollback at the end as lines arrive, unless
;; the user has scrolled up.
(defn view ()
  (ui/column {:font :mono :background 0x101418ff}
    (ui/scroll {:key :scrollback :grow 1 :stick-to-bottom #t :padding 8}
      (ui/lines {:color 0xc8d3e0ff} (term :lines)))
    (ui/row {:padding 8 :gap 8}
      (ui/label {:color 0x7fb2ffff} "$")
      (prompt))))

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
(defn knob (opts) (ui/build Knob opts))

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
(defmethod (ui/handle-event (k Knob) (e ui/PointerDrag))
  (let ((v (- (k :value) (* (e :dy) (/ (- (k :max) (k :min)) 200)))))
    ((k :on-change) (max (k :min) (min (k :max) v)))
    :handled))
```
