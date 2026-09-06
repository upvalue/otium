---
id: lan-syhq
status: open
deps: []
links: []
created: 2026-09-06T22:39:00Z
type: bug
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Centralize and enforce prelude index and count validation

nth, take, drop, range, and related prelude functions do not consistently require integer indexes or counts. Fractional values produce kind-dependent results, including nil-filled array output. get-in also silently ignores extra default arguments.

## Acceptance Criteria

A shared prelude validator enforces documented integer and arity contracts; list and array variants reject the same invalid inputs with function-specific errors; tests cover fractional, nonnumeric, negative, and excess arguments.

