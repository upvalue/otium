---
id: lan-alsd
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
# Count string and buffer length in code points

Generic length returns stored byte counts for strings and buffers while string-length counts UTF-8 code points, contradicting the character-count contract.

## Acceptance Criteria

length, count, and string-length agree for non-ASCII strings; buffer length counts code points; ASCII behavior remains unchanged.

