---
id: lan-nrg1
status: open
deps: []
links: []
created: 2026-10-21T22:25:09Z
type: feature
priority: 2
assignee: Phil
tags: [oct-21-cq, core, runtime]
---
# Bitwise operators

No bitwise operations exist, so lib/otium/sdl.ot:51 flag-set?, manse/keyboard.ot:43, pty.ot:136 wait status and terminal.ot:70 rgba emulate them with quotient/modulo/multiplication. Needs primitives on both runtimes.


## Notes

**2026-10-21T22:43:20Z**

Strawman for discussion: bit-and, bit-or, bit-xor, bit-not, and arithmetic-shift. Use exact integers; negative shift counts shift right with sign extension. Chez can keep bignums while C checks its integer bounds. Confirm names, arities, and overflow behavior before adding primitives.
