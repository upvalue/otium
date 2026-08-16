---
id: lan-cgq3
status: closed
deps: []
links: []
created: 2026-08-16T00:36:57Z
type: task
priority: 2
assignee: Phil
tags: [reader]
---
# reader_set_pos is a dead hook — remove or implement

Only the weak no-op definition exists (reader.cpp:10); no strong definition anywhere. Either remove the hook, its two call sites, and the header decl — or decide source-position tracking is wanted and implement it. Decision needed before code.

