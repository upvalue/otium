---
id: lan-r1ih
status: open
deps: []
links: []
created: 2026-09-06T22:39:19Z
type: task
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Describe native names and arity in one registration table

Native functions repeat arity checks and often report generic helper names such as predicate, rounding, or sign predicate instead of the called function. Registration already has a partial descriptor table.

## Acceptance Criteria

Native descriptors are the single source for public name and arity; repeated checks are removed where practical; errors name the invoked function; extension-native ergonomics remain documented.

