---
id: lan-9oj9
status: open
deps: []
links: []
created: 2026-10-21T22:25:07Z
type: feature
priority: 2
assignee: Phil
tags: [oct-21-cq, core]
---
# Implement case and use it for keyword dispatch

No case macro exists, though lib/otium/format.ot:42 already has a formatter rule for one (keep it once case lands). Replace (cond ((eq? x :a) ...) ...) chains with it: manse/workspace.ot:84 action!, ui/window.ot event!, ui/input.ot dispatch!, ui/native.ot poll-event, and similar. Rely on Chez to compile large dispatches well.


## Notes

**2026-10-21T22:43:20Z**

Strawman for discussion: (case expr ((:a :b) body ...) ((:c) body ...) (else body ...)). Evaluate expr once, compare literal datums with equal?, return nil without a matching clause. Keep else last. Confirm clause syntax and equality before implementation.
