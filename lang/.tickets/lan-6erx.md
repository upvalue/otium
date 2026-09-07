---
id: lan-6erx
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
# Align managed extension objects and inline payloads

Managed objects are aligned to 8 bytes, but ot_ext_obj contains max_align_t and can require 16-byte alignment. UBSan reports misaligned member access in the existing extension test under the semi-space collector.

## Acceptance Criteria

Both collectors return ot_ext_obj and inline payload storage aligned for max_align_t; extension tests pass under address and undefined-behavior sanitizers.


## Notes

**2026-09-06T23:05:10Z**

Both semi and gen passed the full ASan/UBSan test matrix. gc-lint was attempted but the optional LLVM/Clang development headers are not installed.
