---
id: lan-djjx
status: closed
deps: []
links: []
created: 2026-09-06T22:38:41Z
type: bug
priority: 0
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Enforce the actual tagged-integer range

The reader and float conversion validate against intptr_t even though one tag bit reduces the representable signed range. Out-of-range literals silently become unrelated values, and the public ot_make_int helper exposes the same footgun.

## Acceptance Criteria

Explicit fixnum minimum and maximum constants are defined and used by literal parsing, conversions, arithmetic wrapping, and the embedding API; boundary and one-past-boundary tests cover decimal and hexadecimal input.

