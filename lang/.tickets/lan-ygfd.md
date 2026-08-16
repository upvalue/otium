---
id: lan-ygfd
status: open
deps: []
links: [lan-pzh6]
created: 2026-08-16T15:36:19Z
type: chore
priority: 2
assignee: Phil
tags: [loc, gc, code-quality]
---
# promote list_from_stack; replace 4 hand-rolled rooted reverse-folds

cond.cpp:16-21 has the canonical rooted build-list-from-stack-slots helper. The same subtle GC-rooting fold is re-derived at vm.cpp:77-86 (enter_frame rest-arg), vm.cpp:461-470 (Op::List), vm.cpp:486-491 (Op::Append2 tail), data.cpp:368-376 (nat_list). Promote to state.hpp or builtins.hpp and replace. ~20 lines, and it centralizes rooting logic that is easy to get wrong.


## Notes

**2026-08-16T21:39:24Z**

Done. list_from_stack{,_onto} in state.h; all 5 copies gone. Append2 needed the onto-tail variant.
