# Development server

Start a headless server on either runtime:

```sh
./otium serve app.ot
./otium --runtime=c serve app.ot
```

`serve` loads the file, then waits for requests. The file is optional. For a
running UI or an interactive REPL, use `--dev` instead:

```sh
./otium --dev -L shared/lib -L examples examples/ui.ot
./otium --dev --repl
./otium attach
```

The default socket is `.otium/dev.sock` beside the discovered `project.ot`, or
in the working directory when there is no project. `--dev=PATH` selects another
socket. The socket has user-only permissions. A live socket cannot be replaced;
a stale socket is removed on startup. Normal shutdown removes the socket.

`--dev-watchdog=MS` sets the deadline for requests and UI frames. The default
is 2000 milliseconds; 0 disables the watchdog. A timeout pauses execution until
a client sends `:continue` or `:abort`. Foreign calls and primitives must return
before either runtime can pause.

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
{:id 1 :ok #t :value "3" :values ("nil" "3") :output "hello\n"}
```

`:value` is the last result printed with `write`. `:values` contains each form's
printed result, as a list on Chez or an array on C. Loads are not atomic: forms
before an error stay defined. Error responses include `:condition` and captured
`:output`; evaluation errors include the source path and the top-level form's
line. Background output also goes to stdout and is sent as `:output` events.

The shared handler and break loop are in `shared/dev/core.ot`. The native
transport is in `shared/dev/transport.c`. C links it into the launcher; Chez
loads the shared library built by `chez/run`. The legacy `--c.server` framing
remains available for Conjure and uses the shared evaluation handler.

Run `make test-dev` for protocol, interrupt, client, and GC stress checks.
`make test-dev-ui` also requires the native UI dependencies and a working GPU.
