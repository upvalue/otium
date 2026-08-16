---
id: lan-dm0j
status: open
deps: []
links: [lan-pzh6, lan-ius2]
created: 2026-08-16T15:36:17Z
type: bug
priority: 3
assignee: Phil
tags: [gc, compiler, code-quality]
---
# compiler_error raise-and-continue leaves raw Values stale in emit helpers

compiler_error raises (raise_error -> make_table/make_string + handler dispatch, i.e. collects) and then returns so emission continues. Any raw Value in scope at an emit helper's error path silently goes stale. Concrete instances: emit_restart_case compile.cpp:999-1001 ('doc'/'rest' across emit_constant when add_constant/emit_u16 hit their own first-error paths: >65535 constants or >16-bit operand) and emit_quasiquote 876-890. The subsequent array_push stores a stale heap Value into the rooted constants pool; the next GC's visitSlot on that dangling Obj* is UB. Hard to trigger, but the raise-and-continue design makes every emit helper a latent hazard — consider either rooting in these helpers or making compiler_error's collect impossible (pre-built condition).


## Notes

**2026-08-16T21:39:23Z**

Fixed by construction: emit helpers take Ref, so no raw Value survives compiler_error's alloc.
