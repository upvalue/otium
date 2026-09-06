---
id: lan-eru8
status: open
deps: []
links: []
created: 2026-09-06T22:39:42Z
type: feature
priority: 3
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Add a command-line expression evaluation mode

There is no expression option comparable to -e, forcing automation to create a file or drive the REPL or server protocol for one-off evaluation.

## Acceptance Criteria

A documented expression option evaluates source with predictable project and load-path behavior, composes intentionally with files or rejects the combination clearly, and propagates errors through the normal CLI status path.

