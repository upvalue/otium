# Agent interface to the development server

Status: draft, October 18, 2026. Nothing here is implemented yet. It builds on
[the development server](dev-server.md) and changes it in a few places, listed
under [Server changes](#server-changes).

An LLM agent working in a shell should be able to find a running Otium
program, evaluate code in it, reload files it has edited, and look around the program's
namespaces. This spec covers a
command-line client for that, the server changes it needs, and a skill that
tells the agent how to use it.

A skill and a CLI come first. An MCP adapter can come later as a thin layer
over the same client code.

## Client

`tools/otium-dev` is a separate Python program.
It isn't part of the `otium` launcher. Each call connects, sends one request,
prints the result and exits. Agents work one shell command at a time, so a
one-shot client suits them better than a session.

The framing, a small reader for responses (tables, keywords, strings,
symbols, lists, booleans and numbers) and finding servers live in
`tools/otium_tools/dev.py`. `otium attach`, `otium mcp` and the tests use it
too. Reading Otium data in the client avoids settling the JSON question in
the server spec for now.

```
otium-dev ls
otium-dev status
otium-dev eval [--ns NS] (CODE | --file PATH | -)
otium-dev load PATH...
otium-dev interrupt | continue | abort
otium-dev events [--since N]
otium-dev capture OUT.png
```

Any command that takes code takes it as an argument, from a file with
`--file PATH`, or from stdin with `-`. Shell quoting gets awkward for
anything longer than one form, and an agent can write a scratch file and
run it again after editing it.

`eval --file` and `load` differ. `load` reloads a file as its namespace,
the same as `require ... :reload`. `eval --file` evaluates the file's forms
in `--ns`, as if they had been passed as an argument. The client reads the
file and sends its path along with the code, so errors point at lines in
that file. This adds an optional `:path` to `:eval` requests, which
`lib/otium/dev.ot` passes to `%evaluate` in place of `"<dev>"`.

The client resolves paths to absolute paths before sending them. The server
may have been started from a different directory.

Common options: `--pid N` or `--socket PATH` to pick a server,
`--timeout SECONDS` (default 10), and `--raw` to print response tables as
received.

### Output

By default, plain text. Program output first, then the value. An error prints
as

```
error: MESSAGE IRRITANTS...
  at PATH:LINE
```

`load` prints the number of forms evaluated. It stops at the first form that
fails and prints that error. The forms before it stay defined.

Exit codes:

- 0: ok
- 1: the request failed with an Otium error
- 2: no server, or the connection failed
- 3: timed out
- 4: the request paused the program, through the watchdog or an interrupt.
  The request is still running; the client prints how to continue or abort.

A timeout prints `server busy; try otium-dev interrupt`. Without a timeout, an
eval against a program stuck with the watchdog off would hang the agent's
shell.

### Picking a server

If exactly one server is live, use it. Otherwise prefer the one whose cwd or
project contains the current directory. If that still leaves more than one,
fail and print the list, and the agent passes `--pid`.

Compare resolved paths. On macOS `/tmp` is a symlink to `/private/tmp`, so
`getcwd` and `realpath` disagree with paths written the short way.

## Server changes

### Socket location

`--dev` with no path listens on `/tmp/otium-$UID/PID.sock`, creating the
directory with mode 0700. This replaces `.otium/dev.sock` next to
`project.ot`. `--dev=PATH` is unchanged.

The directory must be owned by the user and not writable by anyone else, or
startup fails.

The path is fixed rather than under `$TMPDIR` for two reasons. On macOS
`$TMPDIR` is a long `/var/folders/...` path and socket paths are limited to
104 bytes. And an agent's shell often has a different `$TMPDIR` from the
user's.

Naming sockets by pid means several programs can run with `--dev` at once.

`otium-dev ls` lists the directory, connects to each socket and asks for
`:status`. A refused connection means a stale socket, which the client
removes.

### Status

`:status` returns a table instead of a string:

```scheme
{:id 3 :ok #t :value {:pid 4121 :cwd "/Users/me/code/otium/lang"
                      :runtime :chez :script "examples/ui.ot"
                      :arguments ("--scene" "ui.calculator")
                      :uptime-ms 2460210 :state :running :event 41}
 :output "" :event 41}
```

`:script` and `:arguments` come from `otium.process`. This is a change to
`dev-server.md`, where `:value` is always a printed string. `:status` and
`:events` are the ops whose values are meant for clients rather than people.

### Event buffer

Events are only sent to connected clients, so a one-shot client misses
`:paused` and `:frame-error`. The server keeps the last 100 events, each with
a sequence number in `:seq`. `{:op :events :since N}` returns those after
`N`, and every response carries the latest sequence number in `:event`. The
client can then report events that happened between its calls.

### Value size

`write` of a UI tree or a large table can be megabytes. Requests take
`:max-chars`, default 10000. The server cuts `:value` at that length and adds
`:truncated #t`. Truncation happens on the server so the full text never
crosses the socket. Depth and length limits on printing would be better, but
neither runtime has them yet.

## Introspection

These are Otium functions in `otium.dev` (`lib/otium/dev.ot`), called with
`eval`. They aren't protocol ops, matching the server spec's choice to keep
the protocol small. Starting a server runs `(require (otium.dev :as dev))` in
`user`, so `dev/` works there without setup.

- `(dev/namespaces)`: loaded namespaces, each with the file it came from when
  there is one.
- `(dev/bindings 'NS)`: for each binding defined in the namespace, the name,
  kind (`:function`, `:macro` or `:value`), whether it is public, and its
  docstring, plus `:arity` and `:variadic` for Otium functions. `defonce`
  isn't distinguished from `def`; neither runtime remembers which was used.
- `(dev/source 'NS/NAME)`: the file and line of the definition, or `nil`.
- `(dev/apropos "TEXT")`: qualified names, across loaded namespaces, whose
  name contains the text.
- `(dev/stale-files)`: files of loaded namespaces modified since they were
  loaded, whether by the agent, the user or another agent.

`describe` already exists and stays as it is.

Each runtime provides two primitives and `core.ot` builds the rest:
`%namespaces` returns each namespace's name, file and load time, and
`%bindings` returns a namespace's bindings. The C loader records the file and
load time of each namespace it reads in `main.c`, outside managed objects.
Chez keeps them beside its namespace records. Files loaded with `:load` are
recorded by `core.ot`, keyed by the file's `(ns ...)` form.

`dev/source` scans the namespace's file as it is now, line by line, for a
top-level `def`, `def-`, `defonce`, `defmacro` or `defclass` of the name at
the start of a line. Recording locations at evaluation time would be exact,
but needs changes to both evaluators. Formatted code puts top-level forms at
column 0, and reading the current file gives the line the agent will edit
even when the file has changed since it was loaded.

The UI functions from the server spec (`ui/windows`, `ui/tree-data`,
`ui/capture!`) cover the rest. `ui/tree-data` should take a node path or id so
an agent can ask for one subtree. `otium-dev capture` calls `ui/capture!` and
waits for the file, so the agent can then read the image.

## Skill

`.claude/skills/otium-dev/SKILL.md`, about a page, with details left to
`otium-dev --help`.

It covers:

- When to use it: a program is running with `--dev`, or the agent starts one
  in the background with `./otium --dev FILE`. Run `otium-dev ls` first.
- The loop: edit the file, `otium-dev load FILE`, read errors, inspect, and `capture` when the visual result matters.
- Change code by editing files and loading them, not by evaluating new
  definitions, so the files stay the source of truth. Use `eval` for queries
  and small experiments.
- Keep state in `defonce`.
- The program may be the user's live session. Don't `abort`, quit or send
  input without a reason.
- When paused, look first with `status` and `events`, then choose `continue`
  or `abort`.
- To find things: `dev/namespaces`, `dev/bindings`, `dev/source`,
  `describe`.
- Claude Code's sandbox may refuse connections to sockets under
  `/tmp/otium-$UID/`. If so, the directory needs adding to the sandbox
  settings.

## Tests

- The client against both runtimes: `ls` with zero, one and several servers,
  stale socket removal, `eval` and `load` output and exit codes, `eval` from
  an argument, `--file` and stdin, an error in `eval --file` reporting that
  file's path and line, and a
  timeout against a loop with the watchdog off.
- Events between calls: an event sent while no client is connected is
  returned by `events --since`.
- Truncation at `:max-chars`.
- `dev/bindings` and `dev/source` on a namespace defined in a test file, on
  both runtimes.

## Open questions

1. Should `eval` print events that arrived during the call, or only report
   them through `events`?
2. Is 100 the right size for the event buffer?
