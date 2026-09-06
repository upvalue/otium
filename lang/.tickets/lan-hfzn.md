---
id: lan-hfzn
status: open
deps: []
links: []
created: 2026-09-06T22:39:19Z
type: chore
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Delete the obsolete pre-rewrite REPL implementation

repl/main.c is an unbuilt 828-line implementation against headers that no longer exist. It duplicates the active CLI and REPL in src/main.c and is still scanned by repository tooling.

## Acceptance Criteria

The obsolete file is removed, no build or documentation reference depends on it, and active CLI, REPL, and server tests remain green.

