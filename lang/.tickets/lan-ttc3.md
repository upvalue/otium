---
id: lan-ttc3
status: open
deps: []
links: []
created: 2026-09-06T22:39:19Z
type: task
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Add a reusable rooted temporary-vector abstraction

Compiler, VM, apply, append, macro expansion, and dynamic forms repeatedly create user-sized VLAs plus parallel root-pointer arrays, including special zero-length cases. This duplicates delicate GC code and permits C-stack exhaustion.

## Acceptance Criteria

A reusable bounded or growable rooted vector replaces the repeated VLA patterns; zero-length handling is uniform; large user collections fail controllably or execute without exhausting the C stack; gc-lint and sanitizer tests pass.

