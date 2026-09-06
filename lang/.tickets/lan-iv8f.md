---
id: lan-iv8f
status: open
deps: []
links: []
created: 2026-09-06T22:39:41Z
type: feature
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Define a minimal host surface for command-line scripts

CLI programs cannot inspect command-line arguments, read stdin, access files or environment variables, or launch a process. This makes the current executable an evaluator rather than a practical scripting host.

## Acceptance Criteria

The supported scripting host surface and capability boundaries are explicitly designed; at minimum scripts can receive non-source arguments and perform the approved input and filesystem operations, with focused tests and documentation. Process and environment access are either implemented or explicitly deferred by the design.

