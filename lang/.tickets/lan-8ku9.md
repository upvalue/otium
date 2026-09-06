---
id: lan-8ku9
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
# Unify list array and nil traversal behind a sequence cursor

for-each, apply, string-join, reverse, conversion routines, and related code separately validate and traverse the same three sequence kinds, producing inconsistent errors and mutation behavior.

## Acceptance Criteria

One internal cursor or visitor abstraction defines validation, snapshot semantics, length, and iteration for nil, proper lists, and arrays; duplicated traversal code is reduced and cross-kind contract tests agree.

