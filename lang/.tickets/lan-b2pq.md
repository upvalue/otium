---
id: lan-b2pq
status: closed
deps: []
links: []
created: 2026-08-16T00:36:56Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup, substrate]
---
# Single canonical TableData definition

TableData/TableEntry are defined twice (src/heap.hpp:73 and src/builtins.hpp:44) behind OT_TABLEDATA_DEFINED; whichever include comes first wins and the copies are hand-synced. Keep the heap.hpp one, have builtins.hpp include heap.hpp. Same smell: the native-fn typedef exists 3x (NativeFnPtr in heap.hpp, NativeFn in vm.hpp and builtins.hpp with a 'must match' comment).

## Design

Larger option worth considering while in here: extract the compact dict + val_equal/val_hash from builtins/data.cpp into src/table.cpp. That makes the substrate self-contained and also removes the weak/strong printer_table_* symbol pair. Discuss before choosing scope.


## Notes

**2026-08-16T00:41:23Z**

Canonicalized TableEntry/TableData and NativeFn in src/heap.hpp; builtins.hpp now includes heap.hpp and vm.hpp no longer redeclares NativeFn. Verified with isolated fresh Meson build (24 targets), otium-tests, otium-cli, and git diff --check.
