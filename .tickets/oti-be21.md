---
id: oti-be21
status: open
deps: []
links: []
created: 2026-10-30T04:18:15Z
type: bug
priority: 2
assignee: Phil
tags: [lint]
---
# Lint scratch namespace counter resets on reload

lib/otium/lint.ot:480 (def- scratch-count 0). Reloading otium.lint resets it, so the next run reuses otium.lint.scratch-1, which still holds an earlier file's definitions, and unbound-name findings are missed. Change to defonce. Low priority: lint mostly runs in batch.

