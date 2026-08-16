---
id: lan-xix2
status: open
deps: []
links: []
created: 2026-08-16T00:37:24Z
type: chore
priority: 2
assignee: Phil
tags: [refactor, eval]
---
# Split eval.cpp: move condition and expander natives to builtins/

eval.cpp is 1,307 lines with three separable sections its own comments already delimit: the trampoline evaluator + special forms (~700), condition/restart natives (~250), stage-0 expander + oracle natives (~200). The native sections follow the same nat_*/register_* pattern as the other builtin files. Extract builtins/cond.cpp and builtins/expand.cpp; eval.cpp becomes purely the evaluator.

