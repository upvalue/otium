# Generational collector implementation

Otium's `gen` collector is a single-threaded, stop-the-world collector with a
copying nursery and a mark-sweep old space. Major collection may compact old
space when doing so releases another whole growth chunk. Objects stay
contiguous in both generations; there are no arraylets or separate large-object
representations.

This document describes the implementation in `src/ot-gc-gen.c`. The earlier
design and attribution notes are in [gc-algo.md](../gc-algo.md) and the
[project README](../README.md).

## Source layout

- `src/ot-gc-gen.c` implements allocation, minor collection, major collection,
  side metadata, sweep, and compaction.
- `src/ot-gc.c` implements the selectable semispace collector.
- `src/ot-gc-common.c` owns root-frame bookkeeping, extension finalizer
  dispatch, phase counters, and statistics reset.
- `src/ot-gc-trace.inc` is the shared object and root layout description.
- `src/ot-gc-internal.inc` defines object headers, the collector boundary, and
  `ot_store`.
- `src/otium.c` contains the mutator and routes heap-pointer writes through
  `ot_store`.
- `src/otium.h` exposes configuration and statistics. The collector state is
  otherwise opaque to the runtime.

The build selects exactly one collector with `GC=semi` or `GC=gen`. The
implementation name in source and build flags is `gen`.

## Object representation

Every heap object is eight-byte aligned. A normal header is:

```text
bits 8 and above  object size in bytes
bits 1 through 7 object type
bit 0            zero
```

During minor collection, bit zero distinguishes a normal nursery header from a
forwarding pointer. A forwarded source object contains the destination address
with bit zero set. Old-space headers are not changed for marking or compaction.

An `OBJ_FREE` block uses the same size and type encoding followed by an
intrusive free-list pointer. All other pointers name the exact start of an
object. The collector does not support interior heap pointers.

## Heap layout and defaults

The default host geometry comes from `config.mk`:

| Setting | Default | Meaning |
| --- | ---: | --- |
| `HEAP_INIT` | 1 MiB | Requested initial capacity; nursery and chunk floors may raise it |
| `HEAP_MAX` | 64 MiB | Maximum logical heap size |
| `GC_NURSERY_BYTES` | 2 MiB | Size of each nursery semispace |
| `GC_OLD_CHUNK_BYTES` | 1 MiB | Old-space growth and release unit |
| `GC_LARGE_OBJECT_BYTES` | 256 KiB | Direct-to-old allocation threshold |
| `GC_MARK_STACK_ENTRIES` | 16,384 | Preallocated major mark-stack entries |
| `GC_TIMING` | 1 | Record collection pause time |

The reservation looks like this on a 64-bit host:

```text
nursery reservation: [ current 2 MiB ][ destination 2 MiB ]
old reservation:     [ active chunks ][ inactive range up to old_max ]
side metadata:       [ one entry for every possible old-space card ]
```

`heap_max` includes one nursery semispace and old space. With the defaults,
`old_max` is about 62 MiB. The initial old-space floor is one chunk, so the
initial active capacity is 2 MiB of nursery plus 1 MiB of old space.

The current implementation obtains the full old-space address range and all
card metadata when the state is created. `old_capacity` controls how much of
that range contains valid heap blocks. It grows and shrinks in logical chunks.
This distinction matters when setting a much larger host maximum.

The nursery size is clipped to at most one quarter of `heap_max` for small
heaps. Old space is also limited to `UINT32_MAX` machine words because card
start offsets use 32-bit word indices.

## Old-space side metadata

One card covers 32 machine words, which is 256 bytes on a 64-bit build. Each
possible old-space card has five side-table fields:

| Field | Type | Use |
| --- | --- | --- |
| `remembered` | `uint8_t` | Old object may point into the nursery |
| `starts` | `uint32_t` | Absolute word offset of the first object overlapping the card |
| `marks` | `uint32_t` | One mark bit per word in the card |
| `cumulative` | `size_t` | Live words before the card during compaction |
| `overflow` | `uint8_t` | Minor snapshot or major mark-stack overflow work |

`record_block_start` updates every card crossed by an object or free block. For
the first card it retains the earliest object starting in that card. For later
cards it records the start of the object crossing the boundary. A card scan can
therefore begin at or before the first relevant object even when that object is
larger than a card.

The `overflow` table has two jobs at different times. Minor collection uses it
as a snapshot of the remembered set. Major marking uses it to record work that
did not fit on the bounded mark stack.

## Roots and pointer layouts

Precise roots come from:

- scoped native frames registered with `OT_FRAME`;
- global native slots registered with `OT_GLOBAL`;
- symbol, namespace, core namespace, expander, and type-parent fields on the
  state;
- the VM value stack and bytecode frames;
- active condition handlers, restarts, and dynamic parameter frames.

The extension side list is weak and is deliberately absent from the strong root
walker.

`src/ot-gc-trace.inc` describes every pointer field in every object type. It is
a compile-time template with no include guard. Each phase supplies a direct
slot operation and instantiates a specialized walker:

- nursery evacuation;
- old-space marking;
- compacted-pointer repair;
- optional exact-pointer validation;
- semispace evacuation and validation in the other collector.

There is no visitor function pointer in the per-field path. The compiler emits
direct calls or inlines the slot operation for the selected collector and
phase.

`OBJ_EXT` is pointerless to the strong walker because its `next` field belongs
to the weak extension list. Byte objects, floats, and free blocks are also
pointerless.

## Allocation

`ot_alloc` rounds each request to eight bytes and rejects objects larger than
old space. Small objects use the nursery bump pointer. If the request does not
fit, a minor collection runs and allocation retries once.

Objects at least `GC_LARGE_OBJECT_BYTES`, or larger than one nursery
semispace, go directly to old space. They remain contiguous and may cross any
number of cards and growth chunks. A major compaction may move them.

Old-space allocation uses an active bump region selected from the intrusive
free list. `acquire_region` chooses the largest suitable free block. Subsequent
allocations bump within that region without searching the list. Before a
collection or a new region search, `close_region` turns the unused tail back
into a free block. A tail too small to hold `ot_free_obj` is added to the size
of the preceding live object.

If no region fits, old space grows by enough whole chunks to cover the request.
If allocation still fails, the normal path performs a minor collection,
retries, performs a forced major compaction, and retries once more. Promotion
uses a region reserved before minor evacuation and does not grow old space from
inside the copying loop.

New old-space objects are remembered conservatively before their fields are
initialized. Later minor collections clean the remembered state.

## Write barrier

Every mutation of a pointer field in a heap object goes through:

```c
ot_store(state, owner, slot, value);
```

The store happens first. In a `gen` build, the barrier then checks whether the
owner is in active old space and the new value points into the allocated part
of the current nursery. If both tests pass, it marks the card containing the
start of the owner object.

The barrier marks the owner's start card rather than the card containing the
slot. This keeps large contiguous objects compatible with the remembered-set
scanner. Scanning the start card finds the object and traces its full pointer
layout, even when its last fields are thousands of cards away.

Immediate values, old targets, and stores into young objects do no remembered
set work. VM stacks, native root frames, and fields directly on `ot_state` are
roots rather than heap objects, so they do not use the barrier. In a `semi`
build, `ot_store` compiles to the assignment plus an inline no-op.

The barrier is a generational remembered-set barrier. It is unrelated to a CPU
memory-ordering fence and does not support concurrent marking.

## Minor collection

The nursery uses two fixed semispaces and a watermark to represent age without
adding age bits to object headers.

After a minor collection, `nursery_watermark` points to the end of its survivor
set. Objects allocated after that point are new. At the next minor collection,
a live source object below the old watermark has survived before and is
eligible for promotion. Other live objects copy into the destination nursery.

The collection proceeds as follows:

1. Calculate an upper bound for promotion from the bytes below the watermark.
2. Reserve one contiguous old-space region large enough for that bound. Grow
   old space if possible. If fragmentation prevents the reservation, run a
   forced major compaction and retry.
3. If the full promotion region still cannot be reserved, disable promotion
   for this minor collection. All survivors can still fit in the other nursery
   semispace.
4. Save the source semispace, destination semispace, and old watermark. Copy
   `remembered` into `overflow`, then clear the current remembered set.
5. Evacuate roots. A source object already forwarded resolves through its
   forwarding pointer. A new survivor either copies to the destination nursery
   or bumps into the reserved promotion region.
6. Scan every card in the remembered snapshot. The card-start table locates
   old objects to trace.
7. Cheney-scan destination nursery objects and promoted objects until neither
   scan frontier has work.
8. Rebuild the weak extension list and finalize unreachable nursery
   extensions.
9. Close the promotion region, swap nursery roles, and set the new watermark to
   the end of the surviving nursery objects.

While an old object is being scanned, the collector records it as
`minor_owner`. If one of its fields ends up pointing into the destination
nursery, the object is remembered again. Cards remain dirty only while a
surviving old-to-young edge requires them.

Promotion copies are scanned in the same collection. Their young references
therefore evacuate normally and leave their start cards remembered when
needed.

## Major marking

Major collection begins by closing the current old allocation region and
clearing mark and overflow metadata. It marks from the precise roots, then
traces every allocated nursery object for references into old space.

Scanning all nursery objects is conservative when a major collection runs as a
promotion preflight before the nursery has been evacuated. An unreachable
nursery object may keep an old object alive for one extra major collection. The
next minor collection still reclaims the nursery object safely.

When an unmarked old object is found, the marker sets bits for every machine
word occupied by that object. Marking the full extent serves two purposes:

- the first word is the liveness bit for normal marking and sweep;
- the number of marked words before an address is its compacted word offset.

The object is pushed on the preallocated mark stack. If the stack is full, the
collector sets `overflow` on the object's start card instead. After draining
the stack, it scans marked objects on overflow cards and repeats until no
overflow flags remain. The collector-owned marking path performs no host
allocation.

After marking, `analyze_marks` calculates total live bytes and the end offset of
the highest live object.

## Sweep policy and sweep

The normal policy compares two chunk counts:

```text
sweep chunks   = chunks through the highest live object
compact chunks = chunks required by all live bytes when packed
```

The collector compacts only when packing releases at least one additional
whole chunk. `gc_force_compact` and `--gc-force-compact` override the policy.
Promotion preflight also requests forced compaction when it cannot find a large
enough contiguous region.

Without compaction, sweep walks the original active old-space range in address
order. It retains marked objects, merges consecutive dead objects and old free
blocks, rebuilds the free list and card-start table, and shrinks active capacity
to the chunk containing the highest live object. Capacity never shrinks below
the initial old-space floor.

Sweep remembers the start card of every surviving old object. This conservative
rebuild guarantees that old-to-young edges remain visible. The next minor
collection removes cards whose objects no longer point into its destination
nursery.

## Sliding compaction

Compaction is order-preserving and uses no forwarding headers in old objects.
It proceeds as follows:

1. For each active card, record the number of marked words in all earlier cards
   in `cumulative`.
2. Rebuild the weak extension list. Live old extensions are linked using their
   future addresses, and dead old extensions are finalized.
3. Rewrite old-space pointers in roots, nursery objects, and marked old objects.
4. Walk old space from low to high addresses and `memmove` each marked object to
   its calculated destination.
5. Set active capacity from the packed live byte count, rebuild block-start and
   free-region metadata, and remember every live old object conservatively.

For an old object starting at word `w`, its destination is:

```text
old_base
+ words marked in cards before card(w)
+ words marked below w in card(w)
```

All sizes in that expression are machine words. Since every word of a live
object is marked, the result is the packed address of its first word. Pointer
repair happens before movement while source objects and mark metadata are still
intact. Moving in address order makes downward overlap safe with `memmove`.

A pointer from a live object to an unmarked old object is treated as heap
corruption and aborts during pointer repair.

## Weak extensions and finalization

`state->exts` is a weak side list of `OBJ_EXT` values. Strong reachability comes
from normal roots and object fields.

During minor collection, a nursery extension stays on the list only if it was
forwarded by a strong reference. Unforwarded nursery extensions are finalized.
Old extensions remain for major collection.

During major collection, marked old extensions remain and unmarked old
extensions are finalized. Nursery extensions remain because major collection
does not reclaim the nursery. Compaction relinks live extensions with their
future addresses before movement.

Finalizer dispatch skips released values, inline payloads, null pointers, and
invalid type indices. `ot_destroy` finalizes pointer payloads still present on
the live extension list.

## Pause and memory statistics

Phase statistics are kept separately for:

- semispace full-copy collection;
- generational minor collection;
- major sweep;
- major compaction.

Each phase records a count, total nanoseconds, and maximum nanoseconds. The
generational collector also has a nesting-aware `mutator_pause` counter. A
minor collection may invoke a major compaction during promotion preflight, and
an explicit `ot_collect` performs minor and major collection together. The
outer counter records either case as one complete stop without double-counting
the nested phase.

The minor phase timer starts after promotion-region preparation. A major
collection triggered by that preparation is recorded in its major phase, while
`mutator_pause` includes both the preparation and the minor collection.

The remaining counters report mutator allocations, allocated bytes, copied
bytes, promoted bytes, compacted bytes moved, reclaimed bytes, mark-stack
overflows, used bytes, peak used bytes, active capacity, reservation size,
metadata size, old-space fragmentation, and the largest free region.

`ot_reset_gc_stats` clears workload counters while preserving reservation and
metadata values. It sets the new peak to the current used bytes. The CLI prints
the counters with `--gc-stats` or as one JSON record with `--gc-stats-json`.
Setting `GC_TIMING=0` keeps counts but records zero elapsed time.

## Validation

Defining `OT_GC_VALIDATE` builds another compile-specialized trace pass. When
`gc_stress` is enabled after a collection, it checks:

- no allocation region remains open;
- every strong root names an exact live object start;
- the weak extension list is well formed;
- nursery and old-space blocks have aligned, in-bounds sizes;
- every strong field in every allocated object names an exact live object
  start.

Exact old-object validation starts from the card-start table and walks block
headers. Combining it with collection on every allocation is intentionally
expensive, so the validation build is most useful on bounded cases.

`tests/test_runtime.c` covers a 320 KiB pointer vector spanning more than a
thousand cards, old-to-young stores, a 1.25 MiB byte object spanning growth
chunks, forced compaction, extensions, and mark-stack overflow with an
eight-entry stack. The repeatable sanitizer and non-default geometry matrix is
still follow-up work.

## Mutator invariants

Changes outside the collector must preserve these rules:

1. Root every native `otv` local that must survive an allocation. Allocation
   may move nursery objects, and semispace builds may move every object.
2. Use `ot_store` for every pointer write into a heap object, including writes
   into variable-sized slot and entry arrays. Recompute the owner pointer after
   any call that may allocate.
3. Direct writes to native root frames, VM root arrays, and `ot_state` root
   fields do not need the heap-object barrier.
4. Add every new pointer-bearing object field to `src/ot-gc-trace.inc`.
5. Keep heap pointers exact. A pointer into the middle of a byte or slot object
   cannot survive collection.
6. Do not retain raw pointers returned by `ot_string_bytes` or
   `ot_function_bytecode` across an allocation.
7. Keep object sizes eight-byte aligned and represent every active old-space
   byte as either a live object or an `OBJ_FREE` block before collection.

## Current limits and follow-ups

The current collector has a fixed 64 MiB logical default and allocates its
maximum old-space range and side metadata at startup. Host sizing and lazy
metadata are tracked in [lan-7l89](../.tickets/lan-7l89.md).

Most runtime constructors turn an `ot_alloc` failure into an abort through
`must_alloc`; catchable heap exhaustion and fixed-heap behavior remain in
[lan-emjw](../.tickets/lan-emjw.md).

Major collection is fully stop-the-world. It scans the whole allocated nursery,
and sweep or compaction conservatively remembers every surviving old object.
Major and compaction pause work is tracked in
[lan-7wkw](../.tickets/lan-7wkw.md).

The direct C comparison is in `benchmarks/gc_compare.py`. R7RS collector
comparisons are tracked in [lan-qojh](../.tickets/lan-qojh.md), and an automated
collector validation matrix is tracked in
[lan-s095](../.tickets/lan-s095.md).
