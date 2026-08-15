---
id: lan-dqsf
status: open
deps: []
links: []
created: 2026-08-15T23:20:52Z
type: task
priority: 3
assignee: Phil
---
# REPL polish: multi-line input, quit condition ergonomics

REPL works (links, interactive restart chooser wired via vm_push_handler/vm_pop_handler; quit detection now checks UnwindKind::Quit). Remaining polish: multi-line form accumulation (currently one complete form per line), a (quit) / (exit) native, and deciding what the REPL prints for nil results.

## Acceptance Criteria

Multi-line forms accepted at the REPL; (quit) exits cleanly

