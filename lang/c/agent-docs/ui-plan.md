# First-party desktop UI extension

Status: implementation proposal, September 13, 2026, updated with object
dispatch and scheduling discussions. Work is paused at design. None of the
proposed UI, multimethod, or task APIs below exist yet. Runtime extensions
described as alternatives have not been selected for implementation.

Build a default-enabled CLI extension using SDL3 for windows/input and ThorVG's
OpenGL renderer for graphics. Keep the native integration in `src/ot-ext-ui.c`.
Implement reactivity, widget tables, layout, and the event loop in embedded
Otium modules. Start with one window per running application, on the main
thread. Desktop is the initial target; this proposal makes no embedded memory
guarantee and exposes no backend-selection API.

## Build and dependencies

Use the existing Make build and installed shared libraries discovered through
pkg-config. Do not fetch dependencies during `make`, vendor a graphics stack,
or introduce a second build system for Otium.

Proposed configuration in `config.mk`:

```make
PKG_CONFIG ?= pkg-config
WITH_UI ?= 1
UI_PACKAGES ?= sdl3 thorvg-1
UI_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags $(UI_PACKAGES))
UI_LIBS ?= $(shell $(PKG_CONFIG) --libs $(UI_PACKAGES))
```

The current ThorVG 1.x build generates `thorvg-1.pc`, not `thorvg.pc`.
Its default configuration enables the CPU renderer and omits the C binding.
We need an installation with the GL engine, C API, and font loader enabled.
See the upstream [package definition](https://github.com/thorvg/thorvg/blob/main/src/meson.build)
and [build options](https://github.com/thorvg/thorvg/blob/main/meson_options.txt).

Support one tested ThorVG release/API initially. Select and record that release
during the first rendering spike; do not silently accept an older, incompatible
API under a different pkg-config name. SDL3 3.2 or later is the starting minimum.
This machine currently has SDL3 3.4.10 discoverable through pkg-config and no
ThorVG package discoverable through that tool.

For a release with the inspected 1.x build options, a minimal upstream shared
build would use the following recipe, run separately from Otium's build:

```sh
meson setup build --buildtype=release --default-library=shared \
  -Dengines=gl -Dbindings=capi -Dloaders=ttf -Dextra=
meson compile -C build
meson install -C build
```

Validate this recipe against the selected release before documenting it as
tested. A package-manager installation is equally suitable if it enables the
required features. Use `PKG_CONFIG_PATH` for a custom installation prefix;
normal platform dynamic-library search rules still apply at execution time.
Static dependency packaging is outside the first version.

In Otium:

```sh
pkg-config --modversion sdl3 thorvg-1
make
make WITH_UI=0
```

`WITH_UI=1` is intentional: missing dependencies fail the UI build with an
actionable message, rather than silently producing a different CLI. The message
should mention `make WITH_UI=0`. Run dependency checks only for targets that
need the extension; `make lib`, `make clean`, and core-only builds should work
without graphics packages.

Use a compile/link probe for the C header and actual GL/font entry points we
call. Package metadata alone does not prove engine support. Window creation
also checks GL-canvas initialization and reports unsupported configurations;
do not silently fall back to CPU rendering. Some disabled features may retain
stub symbols, so linking cannot replace the runtime check.

Compile the extension as C, link the CLI against shared SDL3/ThorVG using the
reported library flags. ThorVG's C++ implementation stays in its library. A
successful C link probe verifies that the installation supplies its runtime
dependencies correctly.

Scope UI flags and libraries to the extension and CLI link. Do not add SDL
headers or link dependencies to `libotium.a` or the runtime test executable.
Track CLI feature flags in a generated configuration dependency so switching
`WITH_UI=1` to `0` and back rebuilds `main.o`; the current unconditional relink
alone does not rebuild objects when preprocessor flags change. The former
Raylib feature switch has been removed; graphics use portable SDL FFI.

## Files

| File | Change |
| --- | --- |
| `config.mk` | Default `WITH_UI=1`; pkg-config command, package names, and overridable UI flags. |
| `Makefile` | Optional UI object, target-scoped flags/linking, dependency probe, feature-change tracking, embedded source/font generation, tests. |
| `src/ot-ext-ui.c` | New native module: SDL lifecycle/events, ThorVG paint handles, text measurement, drawing/presentation, embedded-source lookup. |
| `src/otium.h` | Under the internal section and `OT_WITH_UI`, declare UI registration and embedded-source lookup. |
| `src/main.c` | Register the extension; let the CLI loader obtain bundled UI modules before filesystem lookup. |
| `lib/ui.scm` | New public `ui` module: nodes, mounting, ownership integration, basic layout, hit testing, buttons, event loop. |
| `lib/ui/reactive.scm` | New `ui.reactive` module: signals, memos, effects, tracking, batching, cleanup, disposal. |
| `assets/ui/` | Add one redistributable TTF font and its license/attribution; embed it for deterministic default text. |
| `examples/ui/counter.ot` | Standalone example using bundled modules and the default font. |
| `tests/ui-reactive.scm` | Headless reactive semantics tests. |
| `tests/ui-tree.scm` | Headless construction/layout/dispatch/disposal tests using test-supplied measurement and paint hooks. |
| `tests/test_ui.py` | Separate display-backed smoke/integration tests, bounded in time, with input injection and screenshot checks. |
| `tests/test_cli.py` | Bundled imports outside the checkout, disabled build, and CLI lifecycle checks. |
| `tools/gc-lint/run` | Generate embedded inputs and lint the extension with its dependency flags when enabled. |
| `docs/language.md` | Document first-party modules, APIs, lifecycle, and blocking `ui/run` behavior after implementation. |

Keep the one-header invariant enforced by `tests/check_hygiene.py`; do not add
a second source header. Reuse `tools/embed.py`, which already reads bytes,
for source modules and the font. Generated headers live under the build tree.
No README changes. The synchronous UI baseline needs no core evaluator or
collector changes. Resumable tasks and automatic scheduling, discussed below,
would require a separate runtime project.

## Module loading

Register `ui.native` through the existing `ot_register_module` mechanism.
Native module initialization only defines operations; requiring it does not
initialize SDL, load fonts, create threads, or open a display.

Bundle `ui` and `ui.reactive` source bytes into the extension. A small lookup
function returns a borrowed source pointer and length for these two names.
The CLI loader copies recognized sources into host-owned memory, matching its
existing contract: `require_one` frees loaded source after evaluation. For
other names the existing filesystem lookup runs normally.

Use the source-loader path rather than calling `ot_eval_src` from a native
module initializer. The existing loader already propagates source evaluation
failure and restores namespace/loading state. This avoids needing to change
the void-returning native initializer API just to load these modules.

Built-in source takes precedence for these reserved module names. The CLI
works outside the source checkout and needs no `--path` argument. With
`WITH_UI=0`, neither native registration nor bundled source is present; an
ordinary missing-namespace error is sufficient. `libotium.a` remains graphics
independent, as it currently is for the ray extension.

## Counter and invocation

Proposed `examples/ui/counter.ot`:

```scheme
(ns counter
  (:require (ui :as ui)
            (ui.reactive :as rx)))

(define (counter)
  (let ((count (rx/signal 0)))
    (ui/column {:padding 24 :gap 12}
      (ui/text
        (ui/bind
          (lambda ()
            (str "Count: " (rx/read count)))))
      (ui/button
        {:on-click
         (lambda (event)
           (rx/update! count (lambda (n) (+ n 1))))}
        "Increment"))))

(ui/run {:title "Otium counter" :width 400 :height 180}
        counter)
```

```sh
make
./otium --no-project examples/ui/counter.ot
```

No separate UI executable, special evaluator, or UI command-line mode.
`ui/run` returns nil when the window closes, and the script continues. `quit`
and errors keep their existing CLI meaning. Calling `ui/run` again after a
successful close is supported; nested/concurrent runs are rejected initially.
All names above are proposed APIs, not an already runnable example.

## Nodes, ownership, and rendering

Constructors initially return ordinary tables with `:kind`, `:props`, and
`:children`. The `:kind` tag is provisional; see the dispatch discussion below.
Strings supplied as children become text nodes. A node can have one parent;
reject multiple attachment and cycles. Constructors do not attach themselves
to a global parent or allocate GPU objects.

`ui/run` creates an owner before calling the component, keeps its result
reachable, attaches the returned tree, and mounts it. `ui/bind` is an explicit
tagged thunk; mounting creates its tracked property computation. Handler
functions are stored without execution. Components run once per mount.

Keep node data flexible, but use `ui/set!`, `ui/append!`, and `ui/remove!` for
mounted changes so the framework can invalidate layout/paint and manage
lifetimes. Raw table mutations do not automatically participate in reactivity.

Runtime tables retain node rectangles, subscriptions, and opaque native paint
handles. Each native handle identifies an application-owned resource slot;
released slots use a generation check to reject stale handles. Native payloads
hold host pointers/IDs only, never hidden Otium callbacks or managed objects.

The application owns the ThorVG canvas and native paint-resource registry.
Define which references belong to the canvas versus the registry using the
selected ThorVG release's ownership rules; wrappers must not independently
delete objects already released by their canvas. On subtree removal, detach
its paints, release its owned references, and invalidate corresponding slots.

Start with text, buttons, columns, padding/gaps, and fixed/content sizing.
Use the same loaded font and measurement parameters for layout and painting.
Bundle a default font so the counter has no hidden working-directory or system
font dependency. Broader shaping/fallback and editable text are later work.

UI geometry uses logical window coordinates. Native code maps it to drawable
pixels, updates the ThorVG target on resize/scale changes, and invalidates
measurement when necessary. Preserve painter order and clip-aware hit testing.
Retain ThorVG paints and update changed properties; submit a full scene on
each requested repaint initially. No damage-region optimization or public
command-buffer format is required.

## Event loop and runtime

This section specifies the synchronous baseline. The scheduling alternatives
below explain how it could evolve; they are not prerequisites for the counter.

Implement `ui/run` as an ordinary Otium function. Native operations are bounded
leaf calls: open, wait/poll events, measure, update native paints, present,
close. Native code does not invoke Otium event callbacks.

The loop is conceptually:

```text
create native application
establish unwind cleanup and root owner
call component; mount returned tree; settle initial layout; present
while running:
    wait for input or the next repaint/timer deadline, with a bounded timeout
    collect a bounded batch of events
    for each event in order:
        dispatch under its owner's scope and a reactive batch
        settle bindings and required layout before the next hit test
    if repaint is required:
        update native paints; render; synchronize; swap
dispose ownership and release native application
```

Preserve the event returned by `SDL_WaitEventTimeout`, then poll additional
events. Distinguish timeout from error. Cap both events processed per turn
and wait duration (initially 16 ms) so an idle window or input flood does not
prevent VM execution and interrupt checks. This uses SDL's documented
[wait API](https://wiki.libsdl.org/SDL3/SDL_WaitEventTimeout).

Batch each dispatched event rather than the entire input queue: an earlier
event may change visibility/layout that affects a later event's target.
Memos settle before bindings/effects observe derived values. Track dynamic
dependencies afresh, unlink old edges, and bound runaway reactive updates.
Use equality suppression for unchanged scalar values; mutable collections
require explicit signal writes and a documented identity-based policy.

Pointer down establishes press state and capture; pointer up decides button
activation. Tab changes focus; Enter/Space activate the focused button.
Store focus/capture in the Otium application table. Removal clears both when
they reference the removed subtree. Translate SDL key, pointer, committed-text,
and composition events separately, even though editable text is deferred.

The existing VM checks its interrupt flag every 1024 dispatch iterations.
Returning regularly from native wait calls lets that mechanism run. Do not
install UI behavior inside the runtime interrupt hook. Long Otium handlers
block repaint/input processing but remain subject to existing VM interruption;
long native rendering calls cannot be preempted by the VM. Tasks, worker
callbacks, async I/O, and concurrent REPL service are outside this version.

From the REPL, `(ui/run ...)` occupies the current evaluation until the window
closes or evaluation unwinds. It does not keep the prompt or line-oriented
server responsive concurrently. A future cooperative task system can address
that without changing widget constructors now.

Use `unwind-protect` around the entire mounted lifetime, including component
construction. Cleanup must close native resources even if a user cleanup
raises; use nested cleanup protection and continue disposing sibling scopes
before propagating the selected cleanup error. Native partial-open failure
also unwinds any successfully created resources before raising.

Release resources in order: subscriptions/owners; paints/canvas/fonts with
the GL context current; GL context/window; owned SDL subsystem references.
Close is idempotent. GC finalizers are a fallback, not normal window teardown.
Synchronize outstanding ThorVG work before mutating/freeing native resources.
Start without ThorVG worker threads for predictable ownership.

Otium closures and tables live exclusively in traced Otium storage. Root local
managed values in allocation-capable C operations with `OT_FRAME_SCOPED`.
Copy text/event bytes to host storage when native work can outlive a call;
never retain pointers into movable strings. Use the existing host allocator
seam. No collector layout changes should be necessary.

## Implementation order and checks

1. Establish the dependency recipe and GL integration. Build a bounded SDL3 /
   ThorVG window with text, rounded shapes, clipping, resizing, and teardown.
   Verify the exact native target/context arguments on macOS first, then Linux.
   Confirm hardware rendering rather than a CPU bitmap upload. This is the
   gate before committing to the renderer; no untested portability promise.
2. Add the optional CLI extension and embedded-module loading. Verify default
   enablement, actionable missing dependencies, `WITH_UI=0`, core-only library
   builds, and feature toggling without cleaning. Requiring modules must work
   without a display and must not initialize graphics.
3. Implement and test the headless reactive core: conditional dependencies,
   diamond graphs, batching, equality, cleanup on rerun/dispose, nested owners,
   error restoration, and runaway-update reporting.
4. Implement node construction/mounting, basic layout and input, then the
   counter. Validate handler invocation, keyboard activation, and identity
   preservation without reconstructing components on updates.
5. Exercise full lifecycle: close, reopen, error during mount/handler/cleanup,
   quit, SIGINT while idle, node removal, resize and display scaling. A separate
   `make test-ui` requires a real or virtual graphics display and uses bounded
   scripted SDL events plus screenshots. Headless tests remain in `make test`.

Run relevant suites with both collectors and GC stress, plus `make gc-lint`
when the optional LLVM tools are installed. Extend lint coverage to the new C
file. Measure native/GPU memory separately from Otium's heap counters; repeated
mount/unmount should not grow native resources indefinitely.

Defer keyed lists, reactive stores, text editing, accessibility integration,
multiple windows, asynchronous resources, hot reload, and embedded-specific
renderers until the counter and teardown path are reliable. These remain
framework work to plan, not capabilities supplied automatically by SDL/ThorVG.

## Open design: object identity and generic dispatch

Tables remain the preferred object representation for flexibility. Do not
introduce compact records merely on the assumption that tables will be too
expensive. Measure node/subscription costs before changing their representation.

The provisional `:kind` tag identifies behavior, but repeated switches on that
tag would make the framework difficult to extend. Measurement, arrangement,
painting, and event handling should have explicit operation boundaries:

```scheme
(ui/measure node constraints)
(ui/arrange! node bounds)
(ui/paint! node context)
(ui/handle-event! node event)
```

A new primitive widget should be able to supply behavior for these operations
without editing every central dispatch site. Ordinary components such as the
counter are still functions composing existing nodes; each component does not
need a new widget type or methods.

Three possible dispatch identities were discussed:

| Representation | Consequence |
| --- | --- |
| Ordinary `:kind` field | Minimal machinery, but identity is conventional and mutable. |
| First-class descriptor stored in a field | Names can be descriptive while descriptor identity prevents accidental collisions. |
| Descriptor associated with the table by the runtime | Separates type metadata from fields, but requires language/collector decisions. |

Prototype delegation is another possibility, but would also change field
lookup and mutation semantics. A full meta-object protocol could customize
construction, access, inheritance, or method selection. Neither is required
just to make the operations above extensible.

A suggested first experiment is a library-level multimethod: a callable whose
dispatch function produces a key used to select a registered method. It can
start with exact keys and a default method, implemented using closures, tables,
and macros. Illustrative syntax:

```scheme
(define-multi measure
  (lambda (node constraints) (:kind node)))

(define-method measure :button
  (lambda (node constraints)
    ...))
```

Replacing keyword keys with first-class descriptors would not require changing
call sites. Method registration, replacement, missing-method errors, and
introspection need explicit contracts. If dispatch is cached, replacing methods
must invalidate the cache. Descriptor attachment also needs decisions about
copying, equality, printing, and mutation. Prefer preserving `(type node)` as
`:table` and providing separate descriptor inspection if runtime metadata is
introduced; that preference is not a settled language rule.

Exact dispatch on a composite key, such as `[widget-descriptor event-kind]`,
can be explored without inheritance. Hierarchical multidispatch adds ambiguity:
`(button, any-event)` and `(any-widget, pointer-event)` both match a button's
pointer event. Before adding it, specify precedence, explicit preferences, or
an ambiguity error. Do not silently choose registration order.

Next decision: prototype exact-key dispatch and one independently defined
custom widget, then judge whether runtime descriptors or native dispatch are
worth adding. No object system or multimethod syntax has been accepted yet.

## Open design: responsiveness and scheduling

### What the bounded native wait does

The baseline SDL call waits up to its timeout and wakes earlier when input
arrives. A 16 ms timeout does not impose 16 ms of latency on every event. Its
purpose is to return control periodically when idle, allowing VM interrupt
checks and application housekeeping to proceed. It is not a guaranteed
interrupt-response bound: VM polling frequency and intervening native work
also contribute.

An indefinite native wait could leave an interrupt pending until a window
event arrives. A later explicit wakeup mechanism could allow longer idle waits
without sacrificing responsiveness to cancellation or newly runnable tasks.

The timeout only bounds idle waiting. If a handler computes for two seconds,
the baseline loop does not process more input or paint during those two seconds.
Even a preceding write of a "Working" label is not visible until painting runs.
A long Otium computation can remain interruptible through VM polling while its
window is unresponsive. A native operation cannot be preempted by those polls.

### Resumable tasks

Cooperative tasks would preserve execution state and resume after an explicit
yield or scheduler-aware wait. Potential operations are `spawn`, `yield`,
`sleep`, `await`, and cancellation; names and signatures remain open.

Task state includes VM frames/value stack, dynamic parameter bindings, active
handlers and restarts, pending unwind state, and status/result. The heap,
namespaces, and loaded modules remain shared. Every suspended managed value
must be traced and updated by GC. A queued callback that saves its own progress
is a simpler alternative, but requires manually rewriting computations into
steps and does not provide general suspension.

`spawn` should enqueue work rather than execute it inside the spawning handler.
`sleep` suspends its task and records a deadline instead of sleeping the OS
thread. `await` suspends until a result is available. Blocking C calls still
block all tasks unless explicitly adapted or moved to workers. Task failure,
unobserved errors, await propagation, and cancellation need specified behavior.

The scheduler would integrate UI events, runnable tasks, and timer deadlines.
It would wait in SDL only when there is no immediate work, and paint after
settling ready reactive/layout work. Explicit cooperation alone cannot enforce
a time budget: a task that never yields can still monopolize execution.

Prefer component-owned child tasks. Unmount requests cancellation so obsolete
work cannot keep publishing UI results. Cancellation must unwind and run
cleanup, not discard stacks. Decide whether unmount waits for cancellation,
how failed cleanup is reported, and how callbacks/results from a disposed owner
are rejected. Cooperative cancellation cannot forcibly terminate arbitrary C.

### VM time slicing

Automatic slicing uses the same resumable execution state but suspends tasks
when an instruction budget expires at a safe VM boundary. Periodic elapsed-time
checks could supplement that budget. Instruction count alone is not wall-clock
time, and neither check preempts a native call.

The existing VM interrupt poll is a possible scheduling checkpoint. Interrupt
unwinding must remain distinct from suspension: budget expiration saves state
and returns to the scheduler without running cleanup. A later resume continues
at the next instruction. Cancellation is a separate transition that unwinds.

Automatic interleaving changes shared-state semantics. Another task could
observe the state between two table writes that formerly ran uninterrupted.
Single-thread execution avoids simultaneous access, but does not avoid logical
races. A possible `task/atomic` form would defer rescheduling across a short
critical region, servicing a pending scheduling request on exit. Yielding or
blocking inside such a region should be rejected. This form is illustrative;
its error, nesting, and interrupt behavior need a design before implementation.

Reactive graph bookkeeping and batch publication need protected boundaries.
Initially prohibit suspension in tracked computations, batches, and layout/
paint passes, or provide equivalent invariant-preserving implementation rules.
Protection must not become permission to put expensive application work in an
unbounded atomic region. Tracked dependency collection should stay synchronous;
effects may launch tasks with explicit ownership and result publication.

Decide event serialization separately. Scheduling a long task does not imply
that it is correct to run a second click handler while the first is suspended.
A plausible initial policy is short synchronous event handlers with time-sliced
spawned computations. A shared scheduler could eventually keep interactive
evaluation responsive, but requires adapting CLI input/service paths too;
resumable tasks alone do not make the existing blocking REPL concurrent.

### Relationship to continuations and implementation cost

A saved task represents the rest of its computation, so this is restricted
continuation machinery. Each suspension is resumed once under scheduler
control; tasks may suspend again later. This does not commit Otium to exposing
`call/cc`, copying execution state, repeated invocation of one captured state,
or arbitrary cross-task control transfers. General or delimited continuations
would require their own semantic design even if some storage machinery is
reused.

Moving execution fields into a task object is necessary but insufficient.
Current runtime paths include nested C execution and stack-local control
records. A task cannot resume those C frames after suspension returns through
them. Audit all suspension paths and either represent unfinished work in
durable VM-owned records or defer scheduling until the native boundary returns.
Do not preserve pointers to expired C stack frames or root records. Dynamic
handlers/restarts must stay associated with the task that owns their extent;
cross-task restart invocation needs explicit rejection or other defined rules.

This would require changes to `src/otium.c`, internal structures in
`src/otium.h`, GC tracing/root traversal, and runtime tests. Keep SDL out of
the scheduler core: the host should supply waiting/wakeup integration, while
the runtime supplies bounded execution and task state. Exact host APIs remain
to be designed after auditing native suspension boundaries.

If scheduling becomes an early requirement, implement task suspension first,
then explicit yields/timers/cancellation, then automatic budgets. Test dynamic
parameter isolation, GC during suspension with both collectors, tail calls,
handler/restart scope, cancellation cleanup, atomic-region exit on error, task
failure, and an expensive computation alongside repeated UI input/painting.
Include a blocking-native-call test to make the remaining limit visible.

The current plan does not select explicit cooperation versus automatic slicing.
Time slicing is a serious option for interactive Otium, rather than an implicit
promise of the first counter implementation. Resume design work by deciding
whether responsive concurrent evaluation is required before the initial UI
ships, and by auditing the VM's C-stack dependencies.
