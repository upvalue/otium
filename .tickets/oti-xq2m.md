---
id: oti-xq2m
status: open
deps: []
links: []
created: 2026-10-29T06:37:13Z
type: chore
priority: 3
assignee: Phil
tags: [c, signals]
---
# Ignore SIGPIPE in the launcher and raise io-error in the writing thread

Writing to a pipe whose reader has gone kills the process with SIGPIPE, so with threads one broken log pipe ends every thread and no cleanups run. Ignore it in main.c (not the library) and turn EPIPE into an io-error. Children inherit SIG_IGN, so anything that spawns processes must reset it (manse pty.c already does). Deferred 2026-10-29; see scratch/threaded-io.md.

