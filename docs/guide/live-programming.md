# ✨ Live programming

Restarting a program to try a small change can lose the state you were working
with. Otium lets you replace code while keeping that state, so you can change a
function and inspect the result of calling it again in the same process.

Namespace bindings let existing callers find new definitions, while `defonce`
keeps state across reloads. The development server lets you evaluate Otium and
load edited files into a running program.

## Redefinition reaches existing callers

A namespace binding is a named place for a value. Code that refers to that
binding sees its current value, even if the caller was defined earlier.

A saved function value, however, keeps its old code:

```otium
(def (greet name) (str "Hello, " name))
(def (caller) (greet "Ada"))

(def saved greet)
(def callback (fn (name) (greet name)))

(def (greet name) (str "Hi, " name))

(list (caller) (saved "Ada") (callback "Ada"))
; => ("Hi, Ada" "Hello, Ada" "Hi, Ada")
```

Both `caller` and `callback` follow the redefinition. Although the wrapper was
created earlier, it looks up `greet` each time it runs. `saved` holds the
original function.

The same rule applies to qualified references such as `tools/handler`. When a
library retains a callback, pass a forwarding function if it should follow
future redefinitions. Match the handler's arguments, as `callback` does above.

Replacing a definition does not rewrite a call already executing. That call
continues its existing code; subsequent calls through the binding use the
replacement.

## Keep state, replace code

Use ordinary `def` for functions and values you want reevaluated on every load,
and `defonce` for application state you want to preserve.

```otium
(defonce model {:count 0})
(put! model :count 7)

; Evaluating this definition again leaves model alone.
(defonce model {:count 0})
(get model :count)
; => 7

(def step 1)
(def step 2)
step
; => 2
```

Once the binding exists, `defonce` skips its initializer, so editing that
initializer does not reset existing state. Change the live state explicitly when
you need to reset or migrate it.

Keep reloadable definitions separate from startup actions. Reloading a file also
reevaluates its top-level expressions, so opening a window or starting a loop at
top level may repeat that action.

## Reload a namespace

`require` normally reuses a loaded module. Add `:reload` to evaluate its file
again:

```otium
(require otium.extra :as extra :reload)
```

Reloading follows the same rules as evaluating definitions individually:

- Ordinary `def` bindings are replaced; `defonce` state survives.
- Existing namespace callers see replacements; saved function values keep their
  old code.
- Definitions removed from the file remain in the namespace.
- Reload is not atomic. If a form fails, earlier forms have already taken
  effect.
- Dependencies are not automatically reloaded. Reload changed dependencies
  explicitly.

Macro expansions are already part of their callers. After changing a macro,
reload the definitions that use it.

See [Namespaces](namespaces.md) for module names, aliases and load paths.

## Work against a running program

The development server accepts requests over a local socket. Evaluation happens
in the running program, with access to its namespace bindings and state.

From the repository root, start a headless session using the counter example:

```sh
./otium serve examples/reload.ot
```

`serve` loads the file, then keeps the process available for development
requests. For an application that already has its own running loop, start it
with `./otium --dev FILE` instead.

In another terminal, find the process:

```sh
tools/otium-dev ls
tools/otium-dev status
```

With one server, the tool selects it automatically. With several, use `--pid N`
on each command to select the intended process.

### Evaluate and inspect

`eval` prints the value of the last expression. Its default namespace is `user`;
use `--ns` to work in an application namespace:

```sh
tools/otium-dev eval --ns demo.counter '(tick!)'
tools/otium-dev eval --ns demo.counter 'state'
```

The first command changes the running counter. The second reads its state
without changing it.

The `dev` alias is available in `user` for finding definitions:

```sh
tools/otium-dev eval '(dev/namespaces)'
tools/otium-dev eval '(dev/bindings (quote demo.counter))'
tools/otium-dev eval '(dev/source (quote demo.counter/tick!))'
```

`dev/source` gives a file and line to open. It searches the file on disk, rather
than recovering the source of an old function value.

### Edit and load a file

Open `examples/demo/counter.ot`. Its counter state uses `defonce`, and `tick!`
uses ordinary `def`. Change the increment in `tick!`, save, then load the file:

```sh
tools/otium-dev load examples/demo/counter.ot
tools/otium-dev eval --ns demo.counter '(tick!)'
```

The next tick uses the new increment and the existing counter state. Loading by
file evaluates that file's forms; it has the same state-preservation and
partial-failure rules as namespace reload.

Use `eval` for questions and experiments. Put lasting changes in the source file
and load it, so restarting later reproduces your code changes. Read the load
response, since a failed load may have changed part of the program.

For a UI, a successful load does not prove that the next frame succeeds. Check
`tools/otium-dev events` for `frame-error` after reloading, or inspect a capture
with `tools/otium-dev capture /tmp/frame.png`.

## Recover from a stuck evaluation

The development watchdog pauses long-running evaluations or frames after two
seconds by default. You can also request a pause with
`tools/otium-dev interrupt`.

While paused, `eval` and `load` remain available. You can inspect namespace
state and replace definitions, but not inspect lexical locals in suspended
calls.

To leave the pause, choose one of these commands:

- `tools/otium-dev continue` resumes the suspended code.
- `tools/otium-dev abort` unwinds that evaluation, running its cleanup.

Continuing does not restart the suspended call with its new definition. If you
replaced a function containing an endless loop, abort the old call so a later
call can use the replacement.

A client timeout does not cancel the computation. Check `status` before retrying
a command that changes state. Blocking foreign calls must return before Otium
can pause them.

<!-- generated from live-programming.md.spec sha256:136a470ca900ed5365e83a1385e9bcbbf1f6f001521d13b2e8fa3b1fcc5a2a04 -->
