---
id: lan-ug9e
status: open
deps: []
links: [lan-lx92, lan-ngkx]
created: 2026-10-17T05:16:06Z
type: task
priority: 3
assignee: Phil
parent: lan-xfrj
tags: [portability, reader]
---
# Read hexadecimal literals the same way on both runtimes

C reads 0xff, Chez reads #xff. Colour constants in graphics code hit this immediately.

## Acceptance Criteria

One hex syntax reads on both runtimes, with tests.

