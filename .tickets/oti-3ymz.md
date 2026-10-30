---
id: oti-3ymz
status: open
deps: []
links: []
created: 2026-10-30T04:06:34Z
type: task
priority: 2
assignee: Phil
tags: [perf, diagnostics]
---
# Diagnostics re-read the source file for every diagnostic

lib/otium/diagnostics.ot:76: source-line reads and splits the whole file for each diagnostic it renders. tools/lint.ot renders each finding separately, so a file with 200 warnings is read and split 200 times. Fix: cache lines by path (or just the last path), or let the caller pass the text; lint already has it in its context.

