---
id: lan-qn5y
status: closed
deps: []
links: []
created: 2026-09-06T22:39:00Z
type: bug
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Reject unknown namespace clauses and require options

The ns compiler silently skips unrecognized clauses, and require silently skips unknown or malformed options. Typos such as requrie can therefore succeed while doing nothing.

## Acceptance Criteria

Unknown ns clauses, require options, and stray option values produce function-specific catchable errors; all supported spellings and quoted forms remain covered.

