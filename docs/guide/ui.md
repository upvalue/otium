# ✨ User interface

`otium.ui` lets you build graphical windows from buttons, text fields, labels
and containers. You describe what the window should show; the library handles
input, layout and drawing. Application state is ordinary Otium data, so you can
inspect it and change code while the window runs.

The library uses hardware-accelerated rendering. To run a window, you need a
UI-enabled build, SDL3, SDL3_ttf and a working GPU backend. See [How to
run](../how-to-run.md) for setup. Requiring `otium.ui` alone does not open a
window or load the native graphics libraries.

## How a UI fits together

A widget is a part of the interface: a label, an editable field, or a container
holding other widgets. Constructors such as `ui/button` return descriptions, not
the persistent objects on screen.

Your `view` function reads application state and returns a tree of descriptions.
The library matches that tree against its mounted widgets, the persistent
instances that receive events and retain interaction state.

An update follows this path:

1. An input handler changes application data or widget state.
2. The library calls `view` and reconciles its descriptions with the mounted
   tree.
3. Layout assigns bounds, and painting draws the result.

Use `view` to describe the interface and handlers to change application data.

State lives in two places:

- Application data belongs in ordinary bindings, tables or class instances. Use
  `defonce` to preserve it across reloads.
- State belonging to one widget lives in its mounted instance. Text fields
  retain cursor and selection state; scroll containers retain scroll position.
  Custom widgets can declare their own state slots.

## Build an interactive window

This program greets the name entered in a text field. Start with its model and
view:

```otium
(ns guide.ui)
(require otium.ui :as ui)

(defonce model {:name "" :greeting "Hello!"})

(def (greet! text)
  (put! model :greeting (str "Hello, " text "!")))

(def (act! action)
  (when (eq? action :greet)
    (greet! (model :name))
    :handled))

(def (view)
  (ui/column {:grow 1 :padding 20 :gap 12
              :keymap {"Ctrl+Return" :greet} :on-action act!}
    (ui/label {:key :greeting :size 24} (model :greeting))
    (ui/row {:gap 8}
      (ui/text-field {:key :name :grow 1 :autofocus #t
                      :placeholder "Your name" :value (model :name)
                      :on-change (fn (text) (put! model :name text))
                      :on-submit (fn (text) (greet! text))})
      (ui/button {:key :greet :style :accent
                  :on-press (fn () (act! :greet))}
        "Greet"))))
```

The field's `:value` comes from the model. Its `:on-change` callback saves each
edit there, so the next view supplies the updated text. When you press Return,
`:on-submit` receives the text. A button's `:on-press` callback takes no
arguments.

Append the window lifecycle:

```otium
(defonce window
  (ui/open {:title "Greetings" :width 440 :height 180
            :render-on-demand? #t}))

(ui/set-view! window (fn () (view)))
(ui/run)
```

Save both blocks together in an `.ot` file and run it with `./otium` followed by
the filename. Type a name, then press Return or click Greet.

[`open`](/reference/otium.ui/#open) creates the window.
[`set-view!`](/reference/otium.ui/#set-view!) installs its view callback, and
[`run`](/reference/otium.ui/#run) services input and development requests until
the last window closes.

With `:render-on-demand? #t`, handled input and reloads request updates instead
of continuously rebuilding the view. If other code changes the model outside
those paths, call [`invalidate!`](/reference/otium.ui/#invalidate!) with the
window to request an update.

## Children, identity and state

Constructors take an options table first, followed by children for containers. A
child can be a description, a string, a list or array of children, or `nil`.
Strings become labels; sequences are flattened; `nil` is skipped. You can use
`map` for repeated controls and `when` for conditional content.

Treat descriptions and their options as fixed after construction. Return fresh
descriptions when values change.

Matching determines which state survives:

- A keyed child matches by widget class and `:key` among the same parent's
  children.
- An unkeyed child matches by class and sibling position.
- Removing a widget discards its state.

Give repeated or reorderable stateful children stable keys, unique among their
siblings. On a match, new properties replace old ones while widget state
survives.

For reusable groups without their own state, write an ordinary function
returning descriptions. For a group with persistent local state, define a widget
class:

```otium
(defclass Disclosure
  :is ui/Widget
  (open? :init #f :state #t))

(defmethod (ui/build (d Disclosure))
  (ui/column {:gap 8}
    (ui/button {:key :toggle
                :on-press (fn ()
                            (set! (d :open?) (not (d :open?))))}
      "Details")
    (when (d :open?) "This state belongs to the widget.")))
```

Add it to a view with `ui/new`, supplying `Disclosure` and an options table such
as `{:key :details}`. The `build` method receives the mounted instance, so its
callback changes that instance's `open?` slot. The initializer runs when the
instance mounts, not on each view update.

Ordinary slots are caller-supplied properties. Slots marked `:state #t` belong
to the widget and cannot be supplied as constructor options. See [Records and
classes](classes.md) for class syntax.

## Input, actions and focus

Buttons and text fields are focusable by default. Tab moves forward through
focusable controls; Shift+Tab moves backward. Return or Space activates a
focused button. `:autofocus #t` asks a newly mounted widget to take focus if
nothing has it. `:disabled #t` prevents interaction.

Named actions let buttons and shortcuts share application logic. In the greeting
program, `"Ctrl+Return"` maps to `:greet`, and `act!` handles that action.
Keymap lookup starts at the focused widget and walks through its ancestors.
Return `:handled` when an action is consumed; otherwise lookup can continue.

Text input is separate from keyboard shortcuts. Use a text field's `:on-change`
callback for entered text rather than trying to assemble it from key presses.

For custom event handling, specialize
[`handle-event`](/reference/otium.ui/#handle-event). It receives an event table
with a `:kind`, such as `:press` or `:key-down`. Return `:handled` when
consumed, or `nil` to leave it unhandled.

## Layout and text

Rows arrange children horizontally; columns arrange them vertically. Dimensions
and spacing use logical pixels.

In the greeting view, `:padding 20` leaves space inside the outer column, and
`:gap 12` separates its children. The field's `:grow 1` gives it spare
horizontal space beside the button.

Use `:width` and `:height` for fixed dimensions. Otherwise widgets size from
their content and layout constraints. Containers stretch children across their
cross-axis by default; `:align-items :center` centers them instead.

A [`scroll`](/reference/otium.ui/#scroll) container makes overflowing content
scrollable. Give it a bounded height, or let it grow inside a bounded column.
For a log, `:stick-to-bottom #t` follows new content until the user scrolls
away.

Labels wrap by default when width is constrained. Use `:wrap #f` for a single
unwrapped line, `:size` for text size, and `:align` for text alignment. The
bundled font choices include `:ui` and `:mono`. Colors accept palette keywords
such as `:text` and `:muted`, or packed `0xRRGGBBAA` integers.

## Change the running window

Launch your saved program with `./otium --dev` followed by its filename. In
another terminal, use `tools/otium-dev ls` to find the server. With one server
running, the client selects it automatically.

Edit the greeting text, spacing or handler, then run `tools/otium-dev load`
followed by the filename. The loop services the request and refreshes the UI.

On reload:

- `defonce model` preserves entered data.
- `defonce window` avoids opening another window.
- Ordinary `def` replaces functions.
- The view callback calls the live `view` binding.
- `set-view!` runs again; calling `ui/run` while the loop is already running
  does not start another loop.

Matching widgets keep their state through reloads. Keep their class, parent and
keys stable when you want to preserve it.

You can inspect `model` with `tools/otium-dev eval --ns guide.ui 'model'`. Use
`tools/otium-dev events` to check for frame errors after loading changes. Fix
the source and load it again; a successful load alone does not guarantee that
rendering succeeded. See [Live programming](live-programming.md) for the full
workflow.

## Test without opening a window

A headless widget tree lets you test handlers, matching and state without
graphics dependencies. Load the greeting model and view definitions, but omit
the window lifecycle block. Then run:

```otium
(require otium.ui.input :as input)

(def tree (ui/root))
(ui/reconcile! tree (view))

(input/keyboard! tree {:kind :text-input :text "Ada"})
(ui/reconcile! tree (view))

(ui/send! (ui/find-key tree :greet) {:kind :press})
(ui/reconcile! tree (view))

(println ((ui/instance (ui/find-key tree :greeting)) :text))
;; Hello, Ada!

(ui/dispose! tree)
```

Autofocus routes text to the field. [`send!`](/reference/otium.ui/#send!)
delivers a synthetic press directly to the keyed button. Unlike the window loop,
this test explicitly reconciles after input. Turn the final value into an
assertion using the tools in [Testing](testing.md).

Headless tree tests do not check layout or pixels. For rendering checks,
`examples/ui.ot` can run scenes from `examples/ui/` with `--headless`,
`--frames` and `--capture` options passed after `--`. That mode hides the window
but still requires a GPU backend. In your own running window,
[`capture!`](/reference/otium.ui/#capture!) saves the next successfully rendered
frame as PNG or BMP.

<!-- generated from ui.md.spec sha256:ad08d92c292f5d697b142a2e8a1c262aa6820944a8710cd9a84423825b6e48fd -->
