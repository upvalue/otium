# Development server

Status: implemented, October 18, 2026. Usage is covered in the
[language guide](../docs/language-guide.md). This is the stopgap version: single-threaded, built on the interrupt checks both VMs
already have. Green threads are the likely next step and are sketched under
[Later](#later).

A running Otium program, usually a UI, listens on a local socket. Editors, an
`otium attach` command and an LLM (through a small MCP adapter) connect to it,
evaluate code in it, reload files, inspect state and interrupt code that has
stopped responding. The program keeps running when a request fails, and code
stuck in a loop can be paused and examined from outside.

## Why

Today there is `--c.server`, a stdio server in `c/src/main.c` used by the
Conjure plugin. It has several problems for this use:

- C only. Chez is the primary runtime and has no server.
- Only the process that started Otium can talk to it, so nothing can attach to
  a UI that is already running.
- Program output shares stdout with responses. Results and errors are printed
  text with no request ids, so a client has to guess whether a request failed.
- The only way into a busy loop is SIGINT, which needs a terminal or the
  parent process.

The UI demo adds two more. `examples/ui.ot` keeps the tree, renderer and window
in locals inside `main`, so no evaluation can reach them. And an error in
`view`, `build` or `paint` unwinds out of `main`, tears down the window and
exits, which is the opposite of what you want while editing display code.

## Prior art

- SLIME/Swank. The single-threaded communication styles (`:fd-handler`, which
  services requests while the Lisp waits on I/O) are close to this design.
  SLDB, the debugger you land in after an interrupt, is the model for the
  [break loop](#break-loop). The default `:spawn` style uses OS threads, which
  this spec avoids.
- Squeak and Pharo. One OS thread, interrupt checks at sends and backward
  jumps, Cmd-. to suspend the running process and open a debugger on it. When
  the stuck process is the UI process, a new UI process starts so the world
  stays usable. That part is out of scope here; see [Later](#later).
- nREPL, as a warning. It relied on `Thread.stop` to kill busy evaluations,
  which recent JDKs removed. Threads alone don't make code interruptible.

## Scope

In scope:

- A local socket server on both runtimes, with any number of clients.
- A small request protocol with ids and structured errors.
- Requests served between frames by the UI loop, at the REPL prompt, or by a
  headless `otium serve`.
- Pausing misbehaving Otium code, either on request or when a watchdog
  deadline passes, and serving requests while paused.
- Keeping the UI loop alive when a frame fails.

Out of scope:

- OS threads and a multithreaded collector.
- Interrupting a blocking foreign call. The VM never reaches an interrupt check
  while one runs. This is a known limit.
- Recovering from a crash. A supervising process that restarts the program is
  a separate piece of work.
- Inspecting the locals of a paused frame. Paused evaluation sees namespace
  bindings only.
- Authentication. The socket is a Unix socket with user-only permissions.

## Transport

The server listens on a Unix socket. The default path is `.otium/dev.sock`
next to `project.ot`, or in the working directory without a project.
`--dev` turns it on, and `--dev=PATH` picks another path. Starting fails if a
live server already holds the path; a stale socket file is removed.

The listener and connection handling are native code in `native/dev/`, built
the same way as the line editor: C links it directly, and Chez loads it as a
shared library. It does three things:

- Accepts connections and reads frames into per-client buffers, without
  blocking.
- Answers `(dev-pending)`: whether any client has a complete request, and
  whether any of them is an interrupt.
- Hands complete requests to Otium and writes Otium's responses back.

Everything else (dispatch, evaluation, formatting responses) is shared Otium.

Each frame is a 4-byte big-endian length followed by that many bytes of UTF-8
Otium data, printed with `write` and read with the common reader.

## Protocol

A request is a table with an `:id`, chosen by the client, and an `:op`:

```scheme
{:id 7 :op :eval :ns "ui.layout" :code "(count (ui/windows))"}
{:id 8 :op :load :path "examples/ui/layout.ot"}
{:id 9 :op :interrupt}
{:id 10 :op :continue}
{:id 11 :op :abort}
{:id 12 :op :status}
```

Responses carry the request's id:

```scheme
{:id 7 :ok #t :value "1" :output ""}
{:id 7 :ok #f :condition {:type error :message "..." :irritants ("...")
                          :source {:path "..." :line 12}}
 :output ""}
```

`:value` is the result printed with `write`. Values aren't kept on the server
between requests. To keep something, define it.

`:load` evaluates a file form by form, the same as `require ... :reload`, and
reports each form's result. Reload stays non-atomic: forms before a failing
one stay defined.

The server also sends events, which have no id, to every client:

```scheme
{:event :paused :reason :watchdog :elapsed-ms 2140 :ns "ui.layout"}
{:event :resumed}
{:event :frame-error :condition {...}}
{:event :output :text "..."}
```

The protocol stays small on purpose. UI inspection, captures and synthetic
input are Otium functions called through `:eval`, so they can change without
changing the protocol. See [UI support](#ui-support).

Requests are served one at a time, in arrival order, across all clients.

### Output

Output printed while a request runs belongs to that request's `:output`.
Output at other times goes to the process's normal stdout and also to clients
as `:output` events. This needs a hook on the host output sink on both
runtimes. When ports from `specs/io.md` land, it becomes a matter of binding
the current output port around each request.

## Service points

`(dev/service!)` serves every pending request and returns. It doesn't block.
It is called from:

- the UI loop, once per frame, before reconciling
- the REPL, while waiting for a line
- `otium serve FILE`, which loads a file and then waits on the socket

After an evaluation from a client, the UI loop reconciles and redraws as
usual, so changes to `view` show up on the next frame.

Code that never returns to a service point doesn't get its requests served.
That's handled by interrupts.

## Interrupts and the watchdog

Both VMs stop regularly to check for interrupts. The C VM checks a flag at
backward jumps and calls, and with the server running, a timer that counts
process CPU time sets it every 10ms. On Chez, the server installs a timer interrupt handler with
`set-timer`. Ticks are roughly procedure calls and loop iterations.

At these checks, the runtime reads a monotonic clock at most every 50ms of
wall time and calls `(dev-pending)`. It pauses the program when either:

- a client has sent `:interrupt`, or
- the watchdog deadline has passed. The deadline is set when a frame or a
  request starts, 2 seconds by default, configurable with `--dev-watchdog=MS`
  and turned off with 0.

Other requests waiting at that point are left for the next service point,
unless the program pauses.

Pausing never aborts on its own. A slow frame that is doing real work resumes
when a client sends `:continue`.

### Cost and granularity

On Chez, a plain benchmark (`fib 37` and an allocation-heavy loop, not Otium
code yet) showed no measurable cost at 1M ticks between handler calls and
about 1% at 10k ticks. That handler read the clock with `current-time`, which
allocates; the real one should read it with a foreign call. At about 400M
ticks per second, 10k ticks is around 25µs. Use 10k.

On C, the VM already checks the flag, so the server adds nothing to that path.
The handler runs only when the timer has set the flag.

Neither VM can stop inside a single long primitive (a large copy, a huge
`make-vector`). The pause waits for it to return. That is bounded by the
data, so it can't hang. Foreign calls are the same case, except they can
block forever. GC pauses also delay the check, briefly.

## Break loop

When the program pauses, the runtime runs the break loop in place, inside the
interrupt hook, inside `continue` and `abort` restarts. On C the hook is
`ot_set_interrupt_handler`, which both the socket server and `--c.server` use.

The break loop:

1. Sends `:paused` to every client, with the reason and the current namespace.
2. Blocks on the socket, serving requests. `:eval` and `:load` work as usual,
   against namespace bindings.
3. Returns on `:continue` or `:abort`, sending `:resumed`. `:abort` unwinds the
   paused evaluation through the `abort` restart, so `unwind-protect` cleanup
   runs.

A second `:interrupt` while paused does nothing. A client that disconnects
doesn't resume the program; another client can.

The window doesn't redraw while paused. The UI loop is the paused code.

The break loop moves out of `c/src/main.c` into shared Otium, served by the
same request handler as `dev/service!`.

## UI support

The server is useful for UIs only if the UI's state can be reached from an
evaluation. This depends on the window service planned in `specs/ui.md`
(`ui/open`, `ui/set-view!`, `ui/run`), with the loop moving out of
`examples/ui.ot` into the library. Then:

- `(ui/windows)` returns open windows, and each window gives its tree, so
  `ui/inspect` starts somewhere.
- `(ui/tree-data window)` returns the bounds, focus, pressed and disabled data
  that `examples/ui.ot` currently writes as JSON for `--dump-state`.
- `(ui/capture! window path)` saves the next frame. Direct renderers currently
  reject captures, so this renders one frame offscreen.
- `ui/send!`, `input/dispatch!` and `input/keyboard!` already inject events.

Application models stay in `defonce`, which already survives reload.

### Frame errors

`ui/run` catches errors raised while reconciling, laying out or painting a
frame. It skips presenting that frame, sends `:frame-error` to clients, prints
the condition, and tries again next frame. Unchanged code fails the same way
each frame, so it reports a repeated error once until the code changes or the
error does.

Errors in setup and teardown, and `quit`, still end the program.

An error overlay in the window is a nice extra, not part of this spec.

## `--c.server`

`--c.server` stays as a stdio transport into the same request handler, so
Conjure keeps working. The Conjure plugin then moves to the socket, and
`--c.server` is removed.

## Tests

- Both runtimes: eval, load, structured errors, output capture, several
  clients, requests interleaved across clients.
- A tight loop and an allocation-heavy loop each pause within 100ms of
  `:interrupt`, and within 100ms of the watchdog deadline.
- `:continue` resumes with state intact; `:abort` runs `unwind-protect`
  cleanup and leaves the server alive.
- A UI script redefines `view` to raise, checks for `:frame-error` and a live
  window, fixes `view`, and checks the next capture.
- The benchmark suite with `--dev` on and no client connected stays within
  noise of `--dev` off.

## Open questions

1. Otium data or JSON on the wire? Otium data suits Lisp clients and the
   reader exists on both runtimes. JSON is simpler for the MCP adapter, but
   needs a JSON reader in Otium (see `specs/io.md`).
2. Should `:paused` include a stack? Neither runtime exposes backtraces to
   Otium yet. Chez has continuation inspection; C would need new code.
3. Is 2 seconds the right watchdog default for interactive use? It could be
   shorter for frames than for requests.
4. Should `--dev` be on by default for `ui/run` in development?

## Later

Squeak's approach is the natural next step: green threads in one OS thread,
preempted at the same interrupt checks. A paused UI process could then be
debugged from a fresh one, so the window keeps working. On Chez, engines
provide this on top of the same timer. On C, it needs a separate VM stack per
green thread. Neither needs a multithreaded collector.

Real OS threads would only be needed to keep running during blocking foreign
calls. That's a language design question about app concurrency, and should be
settled on its own rather than by this server.
