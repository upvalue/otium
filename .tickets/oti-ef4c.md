---
id: oti-ef4c
status: open
deps: []
links: []
created: 2026-10-27T03:28:29Z
type: task
priority: 2
assignee: Phil
tags: [prototype, performance]
---
# Give map a list fast path

map in lib/otium/core.ot is kind-preserving: it builds its result with builder-like, a for-each closure that set!s a boxed accumulator, and builder-add per element, then builder-finish. On the R7RS deriv port that's half the run time: 2.65s for 2,000,000 iterations, 1.31s with a plain recursive list map defined in the program. femtolisp runs the same benchmark in 0.81s.

filter looks like it has the same shape and probably for-each too.

## Acceptance Criteria

map (and filter, if it shares the pattern) takes a direct path for proper lists and keeps its current behavior for arrays, nil and errors. deriv improves by about the measured amount on the prototype, and the Chez runtime isn't slower.


## Notes

**2026-10-28T00:22:59Z**

Chez runtime removed; the 'Chez runtime isn't slower' criterion no longer applies.
