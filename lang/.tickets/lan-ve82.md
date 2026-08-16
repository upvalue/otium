---
id: lan-ve82
status: open
deps: []
links: []
created: 2026-08-16T00:36:33Z
type: task
priority: 2
assignee: Phil
tags: [runtime, cleanup]
---
# Add a shared GC-safe sequence iterator

apply, string-join, get-in, and other runtime paths each hand-roll traversal over arrays, lists, and nil. Several loops stop when a value is no longer a pair and silently ignore an improper-list tail, while other sequence functions reject the same input. Introduce one shared traversal abstraction so accepted sequence kinds and improper-list behavior stay consistent.

## Design

The iterator must account for the moving collector. Consumers such as get-in can allocate while walking, so a list cursor or collection value cannot live only in an unrooted C++ local. Support arrays, proper lists, (), and nil, and report an improper list distinctly from a non-sequence value.

## Acceptance Criteria

apply, string-join, and get-in use the shared traversal path. Existing behavior for arrays, proper lists, (), and nil is preserved. Dotted list tails produce an error instead of being discarded. GC-stress coverage exercises a consumer that allocates while iterating.

