---
id: on-59zh
status: open
deps: []
links: []
created: 2026-10-28T21:25:13Z
type: task
priority: 2
assignee: Phil
tags: [c, gc]
---
# Check the eight gc-lint warnings that predate the thread work

just gc-lint reports 8 warnings on native at the pre-thread baseline d46b697 and still after the thread work:
- codegen.c and dev.c: 'info' points into rt->global_info and may be stale after a call that can move it (oti_text, dev_push).
- foreign.c, four places: 'state' points into rt->foreign and may be stale (oti_error_condition, pointer_object).
- foreign.c: a pointer into rt->foreign is passed to oti_text, which uses it after a call that can move rt->foreign.
- native.c: 'n' points into rt->native_runtime. That one looks like a false positive: native_runtime is allocated once and never moves.

global_info and foreign do grow, so each needs checking: either reload the pointer after the call, or add an allow entry to c/tools/gc-lint/gc-lint.conf with the reason.

