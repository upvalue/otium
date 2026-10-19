# Development server

Start a headless server on either runtime:

```sh
./otium serve app.ot
./otium --runtime=c serve app.ot
```

`serve` loads the file, then waits for requests. The file is optional. For a
running UI or an interactive REPL, use `--dev` instead:

```sh
./otium --dev examples/ui.ot
./otium --dev --repl
./otium attach
```

The default socket is `/tmp/otium-UID/PID.sock`, so several programs can run
with `--dev` at once and clients can find them by listing that directory. The
directory is created with user-only permissions, and startup fails if it is
owned by someone else or writable by others. `--dev=PATH` selects another
socket. A live socket cannot be replaced; a stale socket is removed on startup.
Normal shutdown removes the socket. A killed process leaves it behind, and
clients remove it when they find nothing listening.

`--dev-watchdog=MS` sets the deadline for requests and UI frames. The default
is 2000 milliseconds; 0 disables the watchdog. A timeout pauses execution until
a client sends `:continue` or `:abort`. Foreign calls and primitives must return
before either runtime can pause.

## otium-dev

`tools/otium-dev` sends one request per call, prints the result as text and
exits, which suits scripts and LLM agents working in a shell.

```sh
tools/otium-dev ls
tools/otium-dev status
tools/otium-dev eval '(+ 20 22)'
tools/otium-dev eval --ns app.state --file scratch.ot
echo '(count (otium.ui/windows))' | tools/otium-dev eval -
tools/otium-dev load examples/ui/calculator.ot
tools/otium-dev events --since 4
tools/otium-dev capture /tmp/frame.png
tools/otium-dev interrupt   # also continue, abort
```

With one server running, commands use it. With several, they use the one whose
working directory contains the current directory, or take `--pid N` or
`--socket PATH`. Values and output are cut at 10000 characters unless
`--max-chars` says otherwise. `--timeout` (default 10 seconds) bounds the wait
for a response. Exit status is 0 for success, 1 for an Otium error, 2 when no
server is reachable, 3 on timeout and 4 when the request paused the program.

## Looking around

`otium.dev` has functions for finding your way around a running program.
Starting a server aliases it as `dev` in the `user` namespace.

```scheme
(dev/namespaces)                    ; loaded namespaces and their files
(dev/bindings 'ui.calculator)       ; name, kind, arity, public?, docstring
(dev/source 'ui.calculator/view)    ; {:file "..." :line 94}
(dev/apropos "caret")               ; qualified names containing "caret"
(dev/stale-files)                   ; files changed since they were loaded
```

`dev/source` looks for the definition in the file as it is now, at the start
of a line, so it finds definitions in formatted code but not ones nested
inside other forms.

## Attach and MCP

`otium attach [PATH]` reads Otium expressions from stdin. `:ns NAME` changes the
evaluation namespace; `:load PATH` reloads a file. `:status`, `:interrupt`,
`:continue`, and `:abort` send the corresponding requests. Ctrl-C sends an
interrupt. Closing the client leaves a paused program paused.

For a single request:

```sh
./otium attach --eval '(+ 20 22)'
./otium attach /tmp/app.sock --op status
```

`otium mcp [PATH]` runs a stdio MCP adapter. It exposes `eval`, `load`, `status`,
`interrupt`, `continue`, and `abort`. Eval accepts `code` and optional `ns`; load
accepts `path` and optional `ns`. A paused evaluation returns the pause event to
the tool caller, which can then inspect the program and continue or abort it.
The adapter does not retain the eventual value of that suspended request.

## UI use

```scheme
(require (otium.ui :as ui))

(defonce model {:count 0})
(def (view) (ui/label {} (str (get model :count))))
(defonce window (ui/open {:title "Counter" :width 400 :height 200}))
(ui/set-view! window (fn () (view)))
(ui/run)
```

`ui/run` services development requests before each frame. Calling it again
while it is running returns immediately, so the same file can be reloaded.
Frame errors skip presentation, report a `:frame-error` event, and leave the
window open. An unchanged error is reported once per window until a frame
succeeds, another error occurs, or code is evaluated through the server.
Setup, teardown, and explicit exits are not frame recovery points.

`ui/windows` returns the open handles. Each handle contains `:tree`, `:window`,
`:renderer`, `:width`, `:height`, and `:frame`. `ui/tree-data` returns bounds and
interaction state. `ui/capture!` schedules a PNG or BMP capture on the next
successful frame, including when the window normally renders directly to the
swapchain. `ui/close!` disposes the tree and closes the window.

## Wire format

Each message is a four-byte big-endian byte count followed by UTF-8 Otium data.
Requests contain `:id` and `:op`. Frames are limited to 4 MiB. Invalid UTF-8,
oversized frames, and clients that stop draining their output are disconnected.
The transport bounds its request queue; decoded requests are serviced serially.

```scheme
{:id 1 :op :eval :ns "user" :code "(println \"hello\") (+ 1 2)"}
{:id 1 :ok #t :value "3" :values ("nil" "3") :output "hello\n" :event 0}
```

`:value` is the last result printed with `write`. `:values` contains each form's
printed result, as a list on Chez or an array on C. Loads are not atomic: forms
before an error stay defined. Error responses include `:condition` and captured
`:output`; evaluation errors include the source path and the top-level form's
line. `:eval` takes an optional `:path`, used in place of `<dev>` in error
locations when the code came from a file. Background output also goes to stdout
and is sent as `:output` events.

`:eval` and `:load` take `:max-chars`, default 10000. Longer values, outputs
and entries in `:values` are cut to that length and the response gets
`:truncated #t`. 0 disables the limit.

`:status` returns a table: `:pid`, `:cwd`, `:runtime`, `:script`,
`:arguments`, `:uptime-ms`, `:state` (`:running` or `:paused`) and `:event`.

Events other than `:output` carry a sequence number in `:seq`, and the server
keeps the last 100. `{:op :events :since N}` returns those after `N`, oldest
first. Every response carries the latest sequence number in `:event`, so a
client that connects for a single request can tell whether it missed anything.

The shared handler and break loop are in `lib/otium/dev.ot`. The native
transport is in `native/dev/transport.c`. C links it into the launcher; Chez
loads the shared library built by `chez/run`. Socket naming, namespace file/load-time
records, and public binding metadata are Otium policy in `core.ot`. Both loaders
notify `record-load!` before evaluating a selected namespace file. Its `defonce`
registry survives reloads, including reloads of the dev module itself. Native
adapters expose namespace/binding facts, procedure parameter data, and POSIX
operations; they do not maintain a second file registry or compute public arity.

The legacy `--c.server` framing remains available for Conjure and uses the
shared evaluation handler.

`just test dev` runs the protocol, interrupt and client checks. `just test 'dev ui'`
also needs the UI build and a working GPU.
