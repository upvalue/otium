---
id: lan-tcre
status: open
deps: []
links: []
created: 2026-08-16T00:36:56Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup, eval]
---
# Shared read-eval loop helper (eval_source)

Four hand-rolled copies of 'read forms until EOF, eval each, stop on Unwind' with subtly different structure: require_load (eval.cpp:316), eval_embedded (vm.cpp:97), run_file (repl/main.cpp:183), and the REPL inner loop. Extract Value eval_source(Vm&, const char* src, u32 len, const char* name); call sites keep only their error-reporting policy. Makes EOF/unwind semantics identical by construction.

