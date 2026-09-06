---
id: lan-c6l5
status: open
deps: []
links: []
created: 2026-09-06T22:39:42Z
type: bug
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Make CLI module search paths predictable for scripts

The executing script directory is not added automatically, and a nonempty OTIUM_PATH prevents the default current-directory path from being added. Scripts therefore resolve sibling modules differently depending on launch directory and environment.

## Acceptance Criteria

The load-path precedence for explicit paths, project paths, environment paths, script directories, and current directory is specified and tested; running a script from another directory can resolve its sibling modules without accidental environment-dependent removal of defaults.

