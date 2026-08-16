---
id: lan-d41h
status: open
deps: []
links: [lan-pzh6]
created: 2026-08-16T20:27:15Z
type: task
priority: 1
assignee: Phil
tags: [embedded, vm, alloc]
---
# Preallocate the value stack and frame vector to their configured caps

cfg.stackSlots (default 4096) and cfg.maxDepth (default 512) are already enforced as hard ceilings at src/vm.c:58-59, but the storage behind them still grows by realloc from a capacity of 8. So the limits are checked, yet the allocation pattern behaves as if the structures were unbounded.

Reserve both at state_create and never realloc after: 4096 * sizeof(Value) = 64 KiB for the stack, 512 * sizeof(CallFrame) = 16 KiB for frames. Cheap, and it takes the value stack out of the steady-state allocation path entirely.

The second payoff is bigger than the first. Once the stack never moves, its base address is stable, which retires the hazard documented at src/state.h:194 ("never cache a Value* -- the stack vec reallocs on push") and lets Slot hold a pointer instead of an index. That interacts directly with lan-pzh6, so worth agreeing on the shape before either lands.

handlers, restarts, paramBindings, loadingNs and nativeModules have no configured caps at all today. They should either get caps in StateConfig and the same treatment, or move to an arena. Leaning toward caps: an embedded target wants a known worst case more than it wants unbounded nesting.

## Acceptance Criteria

State construction reserves stack and frames to cfg.stackSlots / cfg.maxDepth. No realloc of either vector during normal execution, confirmed with the counting-allocator probe.


## Notes

**2026-08-16T21:39:41Z**

Partly done; premise was wrong. Reserving to the cap is the wrong shape -- Lua treats the cap as an error limit. StateConfig now splits initial from ceiling; defaults 1 Mi slots / depth 200000. Slot-as-pointer payoff is off the table.
