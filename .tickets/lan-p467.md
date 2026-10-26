---
id: lan-p467
status: open
deps: []
links: []
created: 2026-10-26T04:10:03Z
type: feature
priority: 2
assignee: Phil
tags: [chez, continuations, design]
---
# Chez support for call-1cc and otium.thread

The C runtime has call-1cc and otium.thread; Chez has neither. call-1cc itself is easy, Chez's call/1cc has the same rules. Threads are the problem.

A thread switch has to run no cleanups, or a thread that yields inside a with block closes its file. Chez's parameterize is built on dynamic-wind and keeps the value in the shared parameter object, so a switch that skips winding leaks one thread's bindings into the next. Tested on Chez 10.4.1: thread B saw thread A's parameterize binding. Engines don't help, they wind on every expiry, can't nest, and share the timer the dev watchdog uses.

The C runtime keeps a (param . value) list per stack and swaps it on a switch. Chez would need the same for defparam/with-params and for current-output-port, current-input-port and current-error-port, which are Chez parameters there. The only real defparam user is building-children in otium.ui. Not checked: whether Chez's exception handler stack leaks the same way.

A no-wind switch on Chez could install the target's winders through #%$current-winders before calling a call/1cc continuation. That worked in a test but it's internal and undocumented.

## Acceptance Criteria

otium.thread passes tests/library/threads-c.ot on Chez, and call-1cc passes continuations-c.ot. Params and ports stay per thread across switches.

