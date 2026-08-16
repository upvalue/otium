---
id: lan-q27r
status: in_progress
deps: []
links: []
created: 2026-08-16T00:37:21Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup, docs]
---
# Remove historical/defensive commentary; document at function level

Sweep all files for comments that narrate the multi-agent development process ('owned by another agent', 'NOTE for the heap agent', 'per interfaces.md', 'INTEGRATION:' markers) or defend a change to a reviewer rather than inform the next reader. Remove them. Where the underlying information still matters (GC discipline, linking constraints for substrate tests, alloc-free contracts), restate it as function- or struct-level documentation on the thing itself — and only where non-trivial. Note agent-docs/interfaces.md has been deleted; any comment citing it must be reworded or dropped.

