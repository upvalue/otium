---
id: oti-flcx
status: open
deps: []
links: []
created: 2026-10-30T02:22:30Z
type: task
priority: 2
assignee: Phil
tags: [performance]
---
# Index non-text constants in oti_constant

oti_constant (c/src/runtime.c:491) scans every constant slot when interning anything other than a symbol or keyword (quoted lists, strings, floats). Compile cost grows with the number of constants loaded, which hurts reloads in a big image. Add a hash on identity (or value for strings/floats) like text_index does for symbols.

## Acceptance Criteria

Interning a constant does not scan the whole table.

