---
id: lan-ce2z
status: open
deps: []
links: []
created: 2026-09-06T22:38:41Z
type: bug
priority: 0
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Handle managed heap exhaustion without aborting

must_alloc prints heap exhausted and aborts, including during ot_create. A CLI heap configuration accepted as valid can therefore terminate with SIGABRT while bootstrapping.

## Acceptance Criteria

Runtime creation reports insufficient heap without aborting, evaluation-time exhaustion uses a documented controlled failure path, and a minimum-heap CLI regression test never terminates by signal.

