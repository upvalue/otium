# UI revisions

Notes from a discussion on 2026-10-23 about per-frame allocation in the UI
framework. The smaller runtime and framework changes below are now implemented;
retained components remain deferred.

## Where we are

Manse's terminal painting is down from 7.3 MB to about 0.3 MB of allocation per
frame. The framework itself now accounts for most of a frame, about 190 KB on a
frame with terminal output. Measured in a headless copy of manse's view:

| Phase | KB | Main cause |
|---|---|---|
| Building the view | 54 | ~18 descriptions at 1-2.4 KB each: options tables, keys arrays, argument lists, `make` with keyword arguments, `row`/`column`/`label` copying options |
| Reconcile | 24 | 752 B per node even for leaves (three temporary tables, closures), plus `with-params` and the generic call in `build` |
| Layout | 59 | `layout-style` returns a new table per node; leaves with no `measure` method still get measured; a new `Bounds` per node |
| Paint | 26 | A new `Bounds` and `gfx/Context` per node; labels measure text again |

Mouse movement probably costs more than this (closer to the ~300 KB seen live),
because each input event rebuilds the view again. That's estimated from the
phase numbers, not measured.

Runtime costs underneath, on Chez:

- A generic call costs ~240 B: a rest list plus a `parameterize` of the next
  method, even though nothing in `lib/` or `manse/` uses `call-next-method`.
- A table literal costs 192 B plus ~118 B per entry.
- `make` with four keyword arguments costs 224 B, 128 B of it the argument list.

An experiment on a copy of the repo (scripts in `/private/tmp/otium-alloc`)
measured:

- 163 KB per frame at the start
- 122 KB after the runtime fixes below (generic calls, `table`, `make`)
- 84 KB after the framework fixes as well (reconcile, `ui/new`, measuring only
  real measurers, `Bounds` in place)

## Decisions

- **Descriptions:** a description's options are nil when empty, and
  descriptions are treated as fixed once built. Manse's `column` stops calling
  `put!` on `(body :options)` and builds a new table instead.
- **Events:** do less work.
  - Only mark the window dirty when something handled the event or hover
    changed.
  - A hover change only repaints.
  - Rebuild the view once before the next event or the frame, instead of after
    every event. Text input switching has to stay correct.
- **Small tables:** use records or classes for fixed-shape framework values
  (measure limits, `layout-style`, options). Leave general tables alone for now.
- **`Bounds` and `Context`:** stay classes, since classes survive live
  reloading.
- **C runtime:** port the runtime fixes there too.

## Leaning, to confirm

- **Next methods:** replace the dynamic `call-next-method` with an explicit,
  lexically bound name, like a named `let`:

  ```lisp
  (defmethod (paint (w FancyBox) g bounds) :next super
    (super w g bounds)
    (draw-border g bounds))
  ```

  - The name is bound to `#f` when there is no next method, which replaces
    `next-method?`.
  - Arguments are passed explicitly.
  - Methods without `:next` pay nothing.
  - Only `tests/classes/checks.ot` uses `call-next-method` today.
  - For comparison, CLOS and Dylan scope the next method lexically, and SBCL and
    Python detect its use in the body automatically. The explicit name avoids
    hidden behavior.
- **Measuring:** layout detects automatically whether a widget's class has a
  `measure` method more specific than `Widget`'s, rather than adding a
  `measures?` generic. The answer must be refreshed when methods are redefined.
- **`Bounds` in place:** probably skip it. It saves ~5 KB a frame. Widgets that
  keep a `Bounds` from `paint` would quietly see it change, and `ui/inspect` and
  `ui/tree-data` return the live object. Retained components would make it
  matter less.

## Direction: retained components, like GPUI

GPUI (Zed) keeps its views across frames. A view calls `cx.notify()` when its
state changes, and a cached view that hasn't notified skips `render`, layout
and paint, reusing the previous frame. (From memory of GPUI, not checked
against its source.)

Otium rebuilds the whole window on any change. The proposal is components that
mark themselves dirty, with only dirty components rebuilt. For example, a manse
column notifies when its terminal gets output. Then the frame repaints that
terminal and skips rebuilding, reconciling and laying out everything else. This
replaces the narrower "repaint without rebuilding" idea and needs its own plan
before building.

## Proposed order

1. Runtime fixes:
   - `:next` for methods
   - fixed-arity `table` and `make`
   - ported to C
2. The event changes and the description options change.
3. Framework fixes:
   - cheaper reconcile
   - `ui/new` no longer copying options
   - automatic measure detection
   - records for limits and `layout-style`
4. A plan for retained components.
5. Re-measure, then decide on a cheaper small-table representation.

## Implementation results

Implemented `:next name` on Chez and C, replacing the dynamic next-method API.
The binding is false at the end of the chain, takes explicit arguments, and
can escape while retaining the old methods. Ordinary Chez generic calls have
fixed-arity paths through four arguments. `table` and `make` have paths through
four key/value pairs; C already receives constructor arguments in arrays.

Descriptions share options without copying; empty framework options are nil.
Manse constructs a new description when adding focus. Reconciliation allocates
key bookkeeping only when needed and skips empty leaf reconciliation. Built-in
styles and measurement limits are records; custom style tables still work.
Measurement detection reads the current method list every layout pass.

Unhandled events no longer request frames. Hover and expose only repaint.
Handled events rebuild before the next event or the frame's step callback;
text input follows focus changes. Bounds and drawing contexts remain classes,
and bounds are still fresh objects.

The same warmed, headless two-terminal fixture on Chez measured these bytes per
phase against a clean copy of the original revision:

| Phase | Before | After |
|---|---:|---:|
| View | 54,321 | 33,109 |
| Reconcile, excluding view | 24,049 | 10,029 |
| Layout | 58,705 | 29,951 |
| Paint with invalidated caches, including submission | 25,713 | 15,857 |
| Full frame | 162,753 | 88,897 |

Full-frame allocation fell about 45%. This fixture uses idle terminals; it does
not measure live mouse traffic or changing terminal output. Run
`./otium --runtime=chez -L . manse/bench-alloc.ot` to repeat the phase measurement.

General small-table representation and retained components remain deferred.
