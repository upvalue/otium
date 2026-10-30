---
id: oti-rlei
status: open
deps: []
links: []
created: 2026-10-30T04:18:17Z
type: task
priority: 2
assignee: Phil
tags: [lint, perf]
---
# Lint rule against string->list in the library

string->list allocates a pair per character; string-index (core.ot) calls (drop start (string->list text)) on every call, in loops in diagnostics, cli, lint and dev. Add a lint rule that warns on string->list inside lib/ (allowable with ;; lint: allow(...)), then remove the existing uses: core.ot (2), syntax.ot (2), compiler/known.ot (1), manse/fonts.ot (1). For string-index, have the C search behind string-contains? return the position.

