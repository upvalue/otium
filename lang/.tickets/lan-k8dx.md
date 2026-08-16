---
id: lan-k8dx
status: open
deps: []
links: []
created: 2026-08-16T00:36:55Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup, builtins]
---
# Consolidate def_native/make_native into one implementation

Three copies of 'construct a native FunctionData' exist: the exported def_native in src/builtins/sys.cpp:17, a private static def_native in src/eval.cpp:1281, and make_native in src/eval.cpp:108. Delete eval.cpp's static copy, implement the shared def_native as make_native + ns_define, and keep a single FunctionData-filling site. Also removes the rooting question in sys.cpp's copy (function not rooted across ns_define; currently safe only because ns_define roots its arg immediately).

