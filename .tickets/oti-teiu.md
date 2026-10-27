---
id: oti-teiu
status: open
deps: []
links: []
created: 2026-10-27T03:30:53Z
type: task
priority: 3
assignee: Phil
tags: [prototype, performance, compiler]
---
# Compile non-tail named lets as loops in the prototype

prototype/compiler.c compiles a named let as a loop in the enclosing frame only when the named let is in tail position. A named let used as a value or followed by more code still allocates a closure, makes a call, and reaches outer variables through upvalues, or boxes them if the body set!s them.

Extending it needs the compiler to tell function tail position from loop tail position. A call to the loop at a loop-tail position becomes moves and a jump; everything else in loop-tail position behaves like non-tail code, and the loop's value is left in dst. That probably means turning the bool tail argument into a level, about 90 sites in compiler.c.

On the R7RS ports CLOSURE is at most about 1% of executed instructions, so the payoff there is small. Check how often the pattern shows up in Manse and the libraries before starting.

