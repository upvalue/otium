---
id: lan-px8q
status: open
deps: []
links: []
created: 2026-10-21T22:25:06Z
type: task
priority: 2
assignee: Phil
tags: [oct-21-cq, core]
---
# One catch-all condition predicate

Catch-everything is spelled (catch ((fn (_) #t) c) ...) about 10 times (ui/window.ot, sqlite.ot, lint.ot, test.ot, diagnostics.ot) and dev.ot:91 defines its own any?. Add one core predicate or allow (catch c ...).


## Notes

**2026-10-21T22:43:21Z**

Strawman for discussion: add condition? as the catch-all predicate, keeping (catch (condition? c) ...). It should recognize all signaled conditions, including host exceptions after normalization. Alternative: (catch c ...), which needs expander syntax. Confirm the public spelling and contract first.
