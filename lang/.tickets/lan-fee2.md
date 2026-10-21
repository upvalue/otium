---
id: lan-fee2
status: open
deps: []
links: []
created: 2026-10-21T22:25:07Z
type: task
priority: 2
assignee: Phil
tags: [oct-21-cq, core, classes]
---
# Class membership predicates

(and (instance? v) (eq? (dispatch-type v) C)) at lib/otium/ui.ot:314 and :417, sqlite.ot:37 and :40; manse/apps.ot:29 uses class-subtype? similarly. Have defclass generate a predicate, or add instance-of?.


## Notes

**2026-10-21T22:43:21Z**

Strawman for discussion: (instance-of? value Class) includes subclasses and returns #f for non-instances. Keep exact dispatch-type checks where exact membership is intended. Avoid generated predicates for now because class names and reloads complicate naming. Confirm argument order and subtype semantics first.
