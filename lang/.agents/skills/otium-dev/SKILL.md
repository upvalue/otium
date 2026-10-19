---
name: otium-dev
description: Inspect and change a running Otium program through its development server with tools/otium-dev. Use when a program (often a UI) is running with --dev or under `otium serve`, or when you need to evaluate code in, reload files into, look around, or capture frames from a live Otium process instead of restarting it.
---

# Working with a running Otium program

`tools/otium-dev` sends one request to a running Otium program and prints the
result. Run `tools/otium-dev --help`, or `--help` after a command, for options.

## Find or start a program

```sh
tools/otium-dev ls
```

If nothing is listed and you need one, start it in the background, for
example (run `just build` first if the UI isn't built):

```sh
./otium --runtime=chez --dev examples/ui.ot -- --scene ui.calculator
```

Use `./otium --runtime=chez serve FILE` for a headless development server.
Prefer an existing process when working on a live session. With one server,
commands use it; with several, selection uses the working directory. Pin the
intended process with `tools/otium-dev status --pid N`, then pass `--pid N`
to subsequent commands. `--socket PATH` selects a custom socket.

Default sockets live under `/tmp/otium-UID/`. If access is denied, inspect the
error and use the environment's permission mechanism if needed; a permission
error alone does not prove the server is absent.

## Look around

```sh
tools/otium-dev status
tools/otium-dev eval '(dev/namespaces)'
tools/otium-dev eval '(dev/bindings (quote ui.calculator))'
tools/otium-dev eval '(dev/source (quote ui.calculator/view))'
tools/otium-dev eval '(dev/apropos "caret")'
tools/otium-dev eval "(describe 'round)"
tools/otium-dev eval '(dev/stale-files)'
tools/otium-dev eval --ns ui.calculator 'model'
```

`dev/` is available in the `user` namespace, which is the default. With
`--ns`, write `otium.dev/` instead. `dev/source` returns the file and line, so
open the file there. It scans the current file on disk, not the source of an
already compiled function. `dev/stale-files` compares modification times with
recorded load times for known namespace files; it does not watch files, compare
contents, or track dependency changes. Missing files are skipped. Check the
load response itself: an empty stale list is not proof that a reload succeeded.

For anything longer than one form, write it to a scratch file and run
`tools/otium-dev eval --file scratch.ot`, or pipe it to `tools/otium-dev eval -`.
Shell quoting gets in the way otherwise.

For UIs: `(otium.ui/windows)` returns the open windows, and
`(otium.ui/tree-data window)` their layout and interaction state.
`tools/otium-dev capture /tmp/frame.png` saves the next frame; read the image
when the visual result matters.

## Change code

Edit the file, then reload it:

```sh
tools/otium-dev load path/to/file.ot
```

Change code by editing files and loading them, so the files stay the source of
truth. Use `eval` for questions and small experiments, not for new definitions
you mean to keep. State in `defonce` survives a reload. Loads are not atomic:
forms before an error may have taken effect, and removed definitions remain.
Namespace callers see redefinitions; saved function values keep their old code.
Use forwarding callbacks such as `(fn args (apply app/handler args))` when they
need to follow reloads. Reload callers after changing macros.

A load only evaluates the file's forms. A UI keeps running and draws its next
frame afterwards, and errors in that frame (in `view` or painting) don't come
back with the load. Before saying a UI change works, check
`tools/otium-dev events` for `frame-error`, or take a capture.

## Pauses

Requests and UI frames have a watchdog, 2 seconds by default. Code that runs
past it pauses: `eval` and `load` exit with status 4 and print how to proceed.
While paused you can still `eval` to inspect state. Then run
`tools/otium-dev continue` to keep going, or `tools/otium-dev abort` to unwind
the paused code. `tools/otium-dev interrupt` pauses a running program on
purpose. Ordinary eval/load requests do not themselves request a pause; if a
busy loop prevents servicing them, send `interrupt` first. Chez uses timer
interrupts and C uses VM hooks, but blocking native/FFI calls must return before
the runtime can pause. Inspection accesses namespace state, not lexical locals
in suspended frames. Continuing resumes the existing code in that frame.

A client timeout does not cancel the server computation. Check status before
retrying a request that mutates state. A paused UI cannot produce its next frame
until it resumes; do not repeatedly request captures while paused.

## The user's program

The program may be the user's live session. Evaluating code changes what they
see, so tell them when you changed state, for example by driving the UI to
reproduce a bug. Don't abort, quit or send input without a reason.

Exit status: 0 ok, 1 Otium error, 2 no server, 3 timed out, 4 paused.

For protocol, launcher, and UI details, read [docs/dev-server.md](../../../docs/dev-server.md) from this skill directory. Commands above run from the repository root.
