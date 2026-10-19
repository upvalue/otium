# Agent interface to the development server

Status: draft, October 18, 2026. Nothing here is implemented yet. It builds on
[the development server](dev-server.md) and changes it in a few places, listed
under [Server changes](#server-changes).

An LLM agent working in a shell should be able to find a running Otium
program, evaluate code in it, reload files it has edited, see whether the next
frame rendered, and look around the program's namespaces. This spec covers a
command-line client for that, the server changes it needs, and a skill that
tells the agent how to use it.

A skill and a CLI come first. An MCP adapter can come later as a thin layer
over the same client code.

## Client

`tools/otium-dev` is a separate Python program, run through `tools/python`.
It isn't part of the `otium` launcher. Each call connects, sends one request,
prints the result and exits. Agents work one shell command at a time, so a
one-shot client suits them better than a session.

The framing and request encoding already exist in `shared/tests/test_dev.py`.
Move that `Client` into `tools/otium_tools/dev.py` so the tests and the
client share it. The new part is a small reader for responses: tables,
keywords, strings, symbols, lists, booleans and numbers. That avoids settling
the JSON question in the server spec for now.

```
otium-dev ls
otium-dev status
otium-dev eval [--ns NS] [CODE]      # CODE from stdin when omitted
otium-dev load [--frame] PATH...
otium-dev interrupt | continue | abort
otium-dev events [--since N]
otium-dev capture OUT.png
```

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

`load` prints one line per form that failed, and nothing for forms that
succeeded, followed by a count.

Exit codes:

- 0: ok
- 1: the request failed with an Otium error
- 2: no server, or the connection failed
- 3: timed out

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
                      :project "/Users/me/code/otium/lang/project.ot"
                      :argv ("examples/ui.ot") :runtime :chez
                      :started 1792345678 :state :running
                      :event 41 :frame-error #f}
 :output ""}
```

`:frame-error` is the last frame error, or `#f` if the last frame succeeded.
This is a change to `dev-server.md`, where `:value` is always a printed
string. Status is the one op whose value is meant for clients rather than
people.

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

### Waiting for a frame

`load --frame` sends the load and then waits for the next completed frame.
It reports either success or the frame error. After editing `view`, "did it
render?" is the usual question, and this answers it in one call. Without a UI
loop running, `--frame` returns once the load does.

This needs a frame counter in `ui/run` that `:status` reports, or a
`:frame` event. The latter fits the event buffer, but sending an event every
frame would flood connected clients. Not settled.

## Introspection

These are Otium functions in a `dev` namespace, called with `eval`. They
aren't protocol ops, matching the server spec's choice to keep the protocol
small.

- `(dev/namespaces)`: loaded namespaces and the file each came from.
- `(dev/bindings 'NS)`: for each binding, the name, kind (function, macro,
  value or `defonce`), arity, whether it is public, and the first line of
  its docstring.
- `(dev/source 'NS/NAME)`: the file and line of the definition.
- `(dev/apropos "TEXT")`: matching names across namespaces, using the lookup
  behind `,help PREFIX`.
- `(dev/stale-files)`: loaded files modified since they were loaded,
  whether by the agent, the user or another agent.

`describe` already exists and stays as it is.

`dev/source` needs both runtimes to record a location when a definition is
evaluated. The reader already tracks lines for conditions. It is the most
useful of these for an agent, since it connects running state back to a
file it can edit.

The UI functions from the server spec (`ui/windows`, `ui/tree-data`,
`ui/capture!`) cover the rest. `ui/tree-data` should take a node path or id so
an agent can ask for one subtree. `otium-dev capture` calls `ui/capture!` and
waits for the file, so the agent can then read the image.

## Skill

`.claude/skills/otium-dev/SKILL.md`, about a page, with details left to
`otium-dev --help`. AGENTS.md gets one line pointing at `tools/otium-dev` for
agents other than Claude.

It covers:

- When to use it: a program is running with `--dev`, or the agent starts one
  in the background with `./otium --dev FILE`. Run `otium-dev ls` first.
- The loop: edit the file, `otium-dev load FILE` (with `--frame` for UI
  code), read errors, inspect, and `capture` when the visual result matters.
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
  stale socket removal, `eval` and `load` output and exit codes, and a
  timeout against a loop with the watchdog off.
- Events between calls: redefine `view` to raise, `load --frame` reports the
  frame error, `events --since` returns it.
- Truncation at `:max-chars`.
- `dev/bindings` and `dev/source` on a namespace defined in a test file, on
  both runtimes.

## Open questions

1. Frame events or a frame counter for `load --frame`?
2. Should `eval` print events that arrived during the call, or only report
   them through `events`?
3. Is 100 the right size for the event buffer?
