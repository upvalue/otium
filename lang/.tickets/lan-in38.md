---
id: lan-in38
status: open
deps: []
links: []
created: 2026-09-06T22:39:19Z
type: task
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Make special-form metadata single-source

Special-form names and aliases are duplicated between the C special-name table, compiler dispatch, and the Otium expander list. Adding or renaming a form requires synchronized edits in several places.

## Acceptance Criteria

A generated or shared definition is authoritative for recognition, aliases, expansion handling, and compiler dispatch coverage; a validation test rejects missing or extra entries.

