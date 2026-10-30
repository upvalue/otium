---
id: opt-nt1b
status: open
deps: []
links: []
created: 2026-10-29T13:55:50Z
type: feature
priority: 3
assignee: Phil
tags: [compiler, optimizer]
---
# Show inlined functions in backtraces

When the native compiler inlines a function (a core function such as first, or a local one), an error inside it is reported in the caller's frame. The message and source position are unchanged, but an uncaught error's "in f" note names the enclosing function and the backtrace has no frame for the inlined one. SBCL behaves the same way. Accepted for now (scratch/optimizer-design.md, Q2). A fix would record inlined calls in the source map so backtraces can show them as frames.

