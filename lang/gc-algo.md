# Generational GC plan

Add a host-oriented generational collector while keeping the existing Cheney
semispace collector as the default and as the comparison baseline. The new
collector is called `gen` in source, build flags, statistics, and documentation.
The upstream work it is based on is credited in the README and covered by the
vendored license notice.

This first cut targets 64-bit laptops and servers. It does not try to solve the
embedded profile at the same time. Heap geometry remains build-time
configuration so a constrained profile can be measured later without adding
platform conditionals throughout the runtime.

## Fixed choices

- `GC=semi` selects the current whole-heap copying collector.
- `GC=gen` selects the copying-nursery and mark-sweep old-space collector.
- `semi` stays the default until both collectors pass the same validation
  matrix and the benchmark results support changing it.
- Otium arrays, byte strings, entry vectors, and code objects stay contiguous.
  There are no arraylets.
- An old-space object may span any number of cards. The side table stores a
  32-bit absolute object-start word for each card, rather than a one-byte
  backward distance that assumes page-sized objects.
- Large objects bypass the nursery and use the same region allocator as other
  old-space objects. They are not a separate non-moving object kind.
- Collection remains single-threaded and stop-the-world.

The host defaults are exposed in `config.mk`:

```make
GC_NURSERY_BYTES ?= 2097152
GC_OLD_CHUNK_BYTES ?= 1048576
GC_LARGE_OBJECT_BYTES ?= 262144
GC_MARK_STACK_ENTRIES ?= 16384
GC_TIMING ?= 1
```

## Collector boundary

`src/otium.c` continues to allocate through `ot_alloc`, request collection
through `ot_collect`, and register roots through `OT_FRAME` and `OT_GLOBAL`.
It owns no collector layout.

The implementation is split as follows:

- `src/ot-gc.c`: semispace implementation.
- `src/ot-gc-gen.c`: nursery, old space, side metadata, sweep, and compaction.
- `src/ot-gc-common.c`: root-frame bookkeeping, finalizer dispatch, shared
  counters, and benchmark counter reset.
- `src/ot-gc-trace.inc`: the single object/root layout description.
- `src/ot-gc-internal.inc`: the private allocation and store boundary.
- `src/otium.h`: public configuration and statistics, plus an opaque collector
  pointer in the internal state definition.

The trace walker is a compile-time template. Each collection phase supplies a
direct slot operation and gets its own specialized object/root walker. There is
no function-pointer call for each pointer field. Semispace stores compile to a
plain assignment; generational stores call the card barrier directly.

Every mutation of a heap pointer uses:

```c
ot_store(state, owner, slot, value);
```

The generational barrier checks whether `owner` is old and `value` is young,
then dirties the card containing the owner's object start. Minor collection
rescans each dirty object and keeps the card dirty only when an old-to-young
edge remains. VM stacks, native frames, and fields on `ot_state` are roots, not
heap objects, so they do not use the barrier.

## Heap layout

The nursery has two fixed, bounded semispaces. New objects use a bump pointer.
An object surviving one minor collection remains in the nursery; it is promoted
on its next survival. A watermark separates those ages without adding age bits
to object headers.

Old space is one aligned reservation activated in `GC_OLD_CHUNK_BYTES` logical
chunks up to `heap_max`. Swept free areas are represented in the heap. The
allocator chooses the largest suitable area, then bump-allocates within it.
It does not search a free list for every pair or binding.

Objects at least `GC_LARGE_OBJECT_BYTES`, or too large for one nursery
semispace, go directly to old space. The allocator and all metadata operations
use the object's full byte extent, so crossing a card or chunk boundary has no
effect on the object representation.

For every 32-word old-space card, side metadata contains:

- one remembered-set byte;
- one 32-bit absolute object-start word;
- one 32-bit mark word, one bit per heap word;
- one cumulative live-word count used to calculate compaction destinations;
- one mark-stack overflow byte.

The mark stack is allocated when the heap is created. If it fills, the marker
sets an overflow flag on the object's start card. It drains the normal stack,
then revisits marked objects on flagged cards. Collection does not call the
host allocator.

## Major collection

Major marking starts from precise roots and scans the current nursery for old
references. Mark bits cover every word of a live object. This makes cumulative
live-word counts sufficient to calculate a compacted address without putting a
forwarding pointer in an old object's header.

The ordinary policy compares the number of chunks needed by a sweep with the
number needed after packing the live bytes. It compacts when packing releases
an additional whole chunk. `ot_config.gc_force_compact` and
`--gc-force-compact` make the decision deterministic for tests and the
fragmentation benchmark.

Compaction is order-preserving:

1. Build cumulative live-word counts.
2. Rebuild the weak extension list and finalize dead extensions.
3. Rewrite roots, nursery references, and fields of marked old objects.
4. Move marked objects down with `memmove`.
5. Rebuild object-start and free-region metadata.

Sweep and compaction both rebuild the remembered set conservatively. The next
minor collection removes cards that no longer contain young references.

## Validation

Both collectors run the existing C and language suites. Collector-specific
coverage adds:

- a contiguous 320 KiB slot vector spanning more than a thousand cards;
- a contiguous 1.25 MiB byte object spanning old-space growth chunks;
- old-to-young stores into that vector;
- promotion and forced pointer-repair compaction;
- the mark-stack overflow path with an eight-entry stack;
- extension movement and exactly-once finalization;
- ASan and UBSan builds.

`OT_GC_VALIDATE` instantiates another direct trace walker and checks that every
root and strong heap field names the start of a live object. It is intended for
bounded stress cases; combining an exact object-start scan with collection on
every allocation is deliberately expensive.

## Benchmarks

`benchmarks/gc_bench.c` provides three in-process workloads:

- `churn`: short-lived lists;
- `mixed`: a large live graph, ephemeral lists, and table mutation;
- `fragmentation`: interleaved live/dead objects followed by forced compaction.

`benchmarks/gc_compare.py` builds both collectors under the same
reserved-plus-metadata budget. It reports fresh-process wall time, timed
workload time, allocation throughput, time in GC, maximum pause, collection
counts by phase, peak object bytes, and the reported reservation-plus-metadata
total. Raw samples can be written to CSV.

Phase counters time copying, minor, sweep, and compaction work independently.
A nesting-aware mutator-pause counter measures the complete stop when one phase
invokes another, so GC percentage and maximum pause do not undercount a nested
major preflight.

The R7RS ports already include `gcbench`, `destruc`, `mperm`, and `takl`. They
remain the next benchmark layer after the direct workloads are stable. Their
runner should eventually consume the same machine-readable GC counters rather
than maintaining a separate statistics path.

## Default decision

Do not change the default in this first cut. Record results on at least one
arm64 laptop and one x86-64 host, at more than one physical-memory budget. The
new collector can become the default after the full language suite is green,
the direct and R7RS GC workloads are repeatable, and no pause or throughput
regression remains unexplained.
