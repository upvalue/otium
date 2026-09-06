---
id: lan-1lqb
status: open
deps: []
links: []
created: 2026-09-06T22:39:42Z
type: feature
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Allow scripts to choose their process exit status

quit and exit accept no arguments and the CLI treats a requested quit as success, so scripts cannot communicate failure or specific status to their caller.

## Acceptance Criteria

A documented API allows a script to request a valid process status; status range and embedding behavior are defined; unwind-protect still runs; CLI tests cover success and failure statuses.

