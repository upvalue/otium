---
id: lan-8wnk
status: open
deps: []
links: []
created: 2026-08-16T00:36:56Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup, builtins]
---
# Unified arity/type-check helpers for natives

Four conventions coexist: need_argc (data.cpp), one_arg (sys.cpp), need_nums/ad-hoc ifs (arith.cpp), need_string/two_strings (string.cpp), inconsistently applied even within a file (nat_string_lt hand-rolls what two_strings does). Move need_argc plus typed helpers (need_string, need_pair, need_nums) into builtins.hpp and use them everywhere (~90 natives). Stretch design option, separate decision: register arity with def_native and check centrally in apply.

