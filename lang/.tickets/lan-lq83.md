---
id: lan-lq83
status: closed
deps: []
links: []
created: 2026-09-06T22:38:41Z
type: bug
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Make get reject unsupported collection types

get currently treats every unsupported first argument as a lookup miss and returns the default, although only nil is specified as total absence and other types should error.

## Acceptance Criteria

get on nil, tables, arrays, and strings retains documented miss behavior; other types raise a catchable error; tests cover calls with and without a default.

