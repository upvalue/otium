# ✨ Live programming

Use the development server to evaluate Otium in a running program, then reload
edited definitions without losing its state.

## Start a live session

Run these commands from the repository root. Start the counter example as a
headless development server:

```sh
./otium serve examples/demo/counter.ot
```

`serve` loads the file and keeps the process available for requests. For a
program that already has its own loop, start it with `./otium --dev` followed by
its filename.

In another terminal, find the server and evaluate a form:

```sh
tools/otium-dev ls
tools/otium-dev status
tools/otium-dev eval --ns demo.counter '(tick!)'
```

On a fresh session, `(tick!)` returns `1`. Each call increments the counter.

With one server, the client selects it automatically. If several are running,
add `--pid N` to commands to select the intended process. Evaluation defaults to
the `user` namespace; `--ns demo.counter` lets you use the counter's names
directly.

## Keep state across reloads

Open `examples/demo/counter.ot`:

```scheme
(ns demo.counter)

(defonce state [0])

(def (tick!)
  (put! state 0 (+ (get state 0) 1))
  (get state 0))
```

`defonce` initializes a binding only if it does not already exist. Reloading
preserves the array and its counter value. Its initializer does not run again,
even if you edit it.

Ordinary `def` evaluates its value again and replaces the binding. Use it for
functions you want to change. Using `(def state [0])` here would reset the
counter on every reload.

## Retain a callback that follows changes

Evaluate these forms in the running program:

```sh
tools/otium-dev eval '(require demo.counter :as counter)
(def saved counter/tick!)
(def callback (fn () (counter/tick!)))'
```

Namespace references are live. The reference to `counter/tick!` inside
`callback` reaches the latest definition each time it runs, even though
`callback` was created earlier.

`saved` holds the function value that existed when it was defined. It keeps that
function's old code.

For callbacks with arguments, forward them through the namespace reference too.
Use explicit parameters when you know the signature; a general forwarding
wrapper has this shape:

```scheme
(fn args (apply counter/tick! args))
```

The counter itself takes no arguments, so the zero-argument wrapper above is
sufficient.

## Edit and reload

In `examples/demo/counter.ot`, change the increment in `tick!` from `1` to `10`,
then load the file:

```sh
tools/otium-dev load examples/demo/counter.ot
tools/otium-dev eval '(callback)'
tools/otium-dev eval '(saved)'
```

If you made exactly one call before editing, these calls return `11` and `12`.
The callback uses the new increment of `10`; the saved function still increments
by `1`. Both access the same preserved state.

To reload a module from Otium, use `require` with `:reload`:

```scheme
(require demo.counter :as counter :reload)
```

You can send that form through the client:

```sh
tools/otium-dev eval '(require demo.counter :as counter :reload)'
```

An ordinary `require` reuses an already loaded module. `:reload` reevaluates the
requested module's forms. It does not recursively reload dependencies that the
module requires normally.

When reloading, keep these limits in mind:

-  Top-level effects run again. Keep startup actions out of files you intend to
  reload repeatedly.
- Definitions deleted from the file remain in the running namespace.
-  Reload is not atomic. If a form fails, earlier forms have already taken
  effect.
-  Macro changes require callers to be expanded again, usually by reloading
  their definitions.

See [Namespaces](namespaces.md) for module names and load paths.

## Check the running result

Use `eval` for inspecting state and trying small changes:

```sh
tools/otium-dev eval --ns demo.counter '(get state 0)'
```

Put changes you intend to keep in the source file, then load it. Definitions
entered only through `eval` will not be there when you restart.

A failed evaluation or load reports an error and leaves the server available.
After a failed load, fix the file and reload it; do not assume the program still
has all its previous definitions unchanged.

For a UI, a successful load does not prove the next frame succeeded. Check for
frame errors:

```sh
tools/otium-dev events
```

## Recover from a pause

The development watchdog pauses long-running evaluations after two seconds by
default. You can also request a pause:

```sh
tools/otium-dev interrupt
```

While paused, use `eval` to inspect namespace state or reload a fix. Suspended
lexical locals are not available. Then choose:

- `tools/otium-dev continue` to resume the suspended code.
- `tools/otium-dev abort` to unwind it.

Continuing resumes the code already executing in that frame; redefining its
function does not replace that suspended execution. A client timeout also does
not cancel a computation, so check `tools/otium-dev status` before retrying a
request that changes state.

<!-- generated from live-programming.md.spec sha256:ba847f2ece4a5b29a73485a524fa08b873ff2e7639c47b9198c34f425e7c4df6 -->
