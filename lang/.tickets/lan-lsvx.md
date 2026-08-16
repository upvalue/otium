---
id: lan-lsvx
status: open
deps: []
links: []
created: 2026-08-16T00:37:23Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup, eval]
---
# Extract repeated micro-patterns in eval.cpp

Three patterns repeat: (a) docstring probe pairp(x) && car_(x).tag==Tag::String && pairp(cdr_(x)) — 5 sites (make_closure, both define branches, defparam, restart-case x2); (b) array-literal-head skip if (pairp(b) && sym_is(car_(b), S.array_)) b = cdr_(b) — 5 sites (bind_param_list, let, with-params, handler-bind, require :refer); (c) intern-name-into-%.*s error dance — ~6 sites across eval.cpp/ns.cpp, wants raise_error_sym(vm, fmt, id). Extract static helpers.

