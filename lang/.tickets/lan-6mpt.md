---
id: lan-6mpt
status: closed
deps: []
links: [lan-c7gk, lan-8ixg]
created: 2026-08-15T23:27:54Z
type: feature
priority: 1
assignee: Phil
---
# Go full Lua: handle-only internal API for GC values

The GC staleness bugs (see lan-c7gk) all have one shape: a C++ local holds a heap Value across an allocating call, the semispace collector moves everything, the local dangles. We adopted the Lua-style stack for rooting but not the property that makes Lua's version safe: in Lua's C API you never hold an object reference at all -- everything is a stack index, so there is nothing to go stale. Our Value is a copyable 16-byte POD passed by value through every signature, so rooting is opt-in convention instead of structure.

This ticket makes the discipline structural: internal code (eval, natives, ns, printer helpers) manipulates rooted stack slots (u32 indices), and a raw Value may only exist transiently between a read from a slot and a use, with no allocation in between.

## Design

Sketch, up for discussion:

- A Slot handle type wrapping (Vm&, u32 index) with get()/set(), plus scope helpers for push/popTo balance. Reads always go through vm.stack so the collector's forwarding is picked up for free.
- Constructors take and return slots: make_pair(vm, carSlot, cdrSlot) -> pushes result. Same for table_put/table_get etc. The current Value-taking signatures either go away or survive only as leaf helpers documented alloc-free.
- eval_tr already keeps form/env/cursor in rooted slots (rootBase..rootBase+2) with re-read discipline; this generalizes that pattern instead of hand-rolling it per special form.
- The native calling convention (args at stack[base..base+argc)) already fits -- natives just stop copying ARG(n) into locals they hold across calls.
- Enforcement idea: make Value non-copyable in a debug build behind a macro, or grep-able naming so a lint can flag raw Value locals in src/. gc_stress + poisoned from-space + ASan stays as the backstop in CI.

Alternative rejected for now (discussed 2026-08-15): switching to non-moving mark-sweep, which deletes the staleness class and avoids semispace's 2x residency on small targets, but reverses the settled collector decision. If that gets re-opened it supersedes this ticket.

## Acceptance Criteria

No raw heap Value locals held across allocating calls anywhere in src/ (grep/lint clean by whatever enforcement we pick); full conformance suite green against the gc_stress+ASan build; interfaces.md updated to the slot-based signatures

## Notes

**2026-08-16T00:17:09Z**

Implemented (2026-08-15): Slot/Scope handle API in vm.hpp; eval.cpp/ns.cpp/vm.cpp/reader.cpp/builtins/*/repl converted — no raw heap Value locals across allocating calls; shared ARG macro in builtins.hpp; alloc-free contract for table_get/table_put/array_* documented in heap.hpp and interfaces.md. Decision with Phil: keep Value returns + Unwind sentinel (native signature unchanged); enforcement mechanism (lint / debug non-copyable Value) DEFERRED for now — revisit later, gc_stress+ASan remains the backstop. resolve_var_impl's Value* err out-param replaced with a bool (the out-value was only ever the immediate Unwind sentinel).

**2026-08-16T00:22:27Z**

Acceptance met (2026-08-15): no raw heap Value locals across allocating calls in src/ + repl/ (structural via Slot/Scope; verified by gc_stress+ASan gate, 8/9 with only the non-GC lan-t86k failure); interfaces.md updated to the handle discipline. Enforcement mechanism deferred by decision — reopen or ticket separately when wanted. Closing.
