---
id: lan-p4j2
status: open
deps: []
links: [lan-pzh6]
created: 2026-08-16T21:55:15Z
type: task
priority: 1
assignee: Phil
tags: [gc, code-quality, refactor, agents]
---
# Split src/ into a core layer that may do pointer work and a surface layer that cannot

The rooting convention (Ref, OT_SCOPE, re-derive interior pointers) currently applies uniformly to all 6400 lines of src/. Nothing marks which files actually need that care, so an agent editing builtins/arith.c has to carry the same GC model as one editing heap.c. That is the cost we are paying: not unsafety so much as every file demanding core-level attention.

The API makes it worse by handing out exactly the things the convention forbids you to hold:

- The four functions that allocate take raw Values, not handles: array_push and table_put (heap.h:318-320), array_reserve (heap.h:305), buffer_append (heap.h:277). So the API most likely to invalidate your handle is the one that will not accept a handle. This is not hypothetical: a test rooted a copy of a table Value and passed the stale local to table_put, which segfaulted under OT_GC_STRESS. Taking Ref would have made it a compile error.
- array_items, table_entries, slots_items, bytes_items, string_bytes and code_bytes (heap.h:225-284) return bare pointers into the GC heap with no lifetime attached. Every rooting bug found during the Ref conversion was this shape -- a hoisted ArrayData* or TableData* across a mutation.

Both are legitimate inside the runtime core. Neither should be reachable from a builtin.

## Design

Split src/ into two layers and enforce the boundary mechanically. The goal is navigability: an agent should be able to tell, from the file it has open, whether it has to think about the collector at all.

## Core

Allowed to take interior pointers, hold raw Values across allocations, and touch heap internals. Expected to justify each instance at the site. Candidates: heap.c/h, vm.c, state.c/h, code.c, compile.c, gc-facing parts of eval.c.

## Surface

Everything else: builtins/*, reader.c, printer.c, ns.c, extensions. Gets a total API where the unsafe move is unavailable rather than merely discouraged. A surface file should be writable by someone who does not know the collector moves objects.

Surface needs, at least:
- Handle-taking mutators: array_push(State*, Ref, Ref), table_put(State*, Ref, Ref, Ref), array_reserve(State*, Ref, u32), buffer_append(State*, Ref, ...).
- Element access without exposing storage: array_ref(Value, u32), array_set(State*, Ref, u32, Ref), and the same for tables.
- Iteration that survives a collection mid-loop. SeqIter already does this; extend the pattern to tables rather than exposing entriesLen/entries.
- String and buffer byte access as a copy-out or a callback, not a pointer, unless the caller is core.

## Enforcement

tests/check_hygiene.py already scans src/ with a RULES dict and a per-file EXEMPT map, so the machinery exists. Add a rule banning the core-only identifiers (array_items, table_entries, slots_items, bytes_items, code_bytes, code_consts, string_bytes, as_slots, as_entries, as_bytes, heap_alloc, tempRoots) outside an explicit core file list. The list is the layer definition; keep it in one place and make adding to it a deliberate act.

Two open questions worth settling before starting:

1. Where does compile.c go? It is the largest file (1414 lines), it holds bytecode buffers, and it was the single biggest source of rooting bugs -- which argues both for core (it needs the freedom) and against (it is where mistakes actually happen). Splitting the analysis pass, which is already allocation-free, from the emit pass may be the real answer.
2. How much does the pointer-free surface API cost in hot paths? array_items(v)[i] in a loop becomes a call per element. Probably fine given the inline accessors, but measure before converting the array-heavy builtins.

Deliberately out of scope: staleness detection at runtime (epoch counters, poisoned handles). Phil's call -- the goal is a codebase that is easy to move around in, not a better debugger for the current one.

## Acceptance Criteria

- src/ has an explicit, single-source-of-truth list of core files; every other file is surface.
- check_hygiene.py fails when a surface file names a core-only identifier. Verified by planting a violation, as the raw-allocator rule was.
- array_push, table_put, array_reserve and buffer_append take Ref for the collection and for any heap-valued argument. Passing a raw Value does not compile.
- No surface file takes an interior pointer into the GC heap.
- A builtin can be written without knowing the collector moves objects: element access, iteration and string/buffer reads are all available without a raw pointer.
- AGENTS.md says which layer is which and what changes between them, replacing the current advice that applies everywhere equally.
- Suite green, including under OT_GC_STRESS.

