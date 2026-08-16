---
id: lan-iuyk
status: open
deps: []
links: [lan-70gb, lan-pzh6]
created: 2026-08-16T15:38:45Z
type: feature
priority: 1
assignee: Phil
tags: [gc, performance, embedded]
---
# Replace semispace Cheney GC with Compressor-style bitmap mark-compact

The current collector (`src/heap.cpp` / `src/heap.hpp`) is a semispace Cheney
scavenger: `collectInto` mallocs a full to-space, copies live objects
breadth-first, and frees from-space. That means worst-case footprint is 2x the
heap, transiently, at exactly the moment the system is under memory pressure.
For the embedded, long-running target that is the wrong shape. Replace it with
a single-space sliding mark-compact using Compressor-style forwarding
(Kermany & Petrank 2006): mark into a side bitmap, derive forwarding addresses
from bitmap arithmetic instead of a per-object field, then slide live objects
down in address order.

What this buys:

- Peak memory ~= heapSize + ~2% fixed overhead (mark bitmap + offset table),
  instead of 2x during every collection. Steady-state collections allocate
  nothing.
- Bump allocation, allocation order, and full compaction are all preserved, so
  a heap that runs for months does not fragment.
- The `Obj::forward` pointer is deleted entirely, shrinking the header from 24
  to 16 bytes. Meaningful on a heap full of pairs (16-byte payload).
- Marking writes to a compact bitmap rather than scattering writes across
  every live header; forwarding lookup is a load plus a popcount.

Alternatives considered and rejected: LISP2 (works, but keeps the forwarding
word and an extra full-heap address pass); threaded compaction (Jonkers/Morris
-- zero space but cache-hostile and ugly with tagged Value slots); Immix
(mark-region -- excellent throughput but more machinery, only mostly
compacting, and its natural payoff is a generational design we are explicitly
deferring). Stop-the-world pauses are acceptable; pause times will be somewhat
longer than Cheney on young-garbage-heavy workloads and that trade is
accepted. Generational is out of scope; if it ever happens the cost is a write
barrier audit over every Value store into a heap object, which nothing here
makes worse.

## Design

Fixed side structures, allocated once at heap creation (not per collection):

- Mark bitmap: 1 bit per 8-byte granule of the heap => spaceSize/64 bytes
  (1 MiB at the 64 MiB cap). Objects are 8-aligned already (`align8`), so a
  granule index uniquely identifies an object start. Mark bit set on the
  object's first granule.
- Offset table: one u32 per block (block = 512 bytes of heap): the destination
  address (as a heap offset) of the first live object *starting* in that
  block. All arithmetic fits u32 since spaceSize <= 64 MiB.
- Mark stack: bounded `Vec<Obj*>`. On overflow, set an overflow flag and
  rescan the heap for marked-but-untraced objects until a pass completes with
  no overflow ("mark until no overflow"). Deutsch-Schorr-Waite pointer
  reversal is explicitly deferred; the bounded-stack-plus-rescan fallback is
  simpler and only slow in the pathological case.

Collection phases (replacing the single Cheney scan in `collectInto`):

1. **Mark**: run root walkers and `tempRoots` with a visit function that sets
   the bitmap bit and pushes unmarked objects; drain the mark stack, tracing
   per-type Value fields (the existing `switch` in `collectInto` moves here
   unchanged). O(live).
2. **Build offset table**: one linear pass over the bitmap accumulating live
   bytes; write each block's first-live-object destination. O(heap/64).
3. **Fix roots**: re-run root walkers and tempRoots with an update visit
   function. Forwarding address of an object at offset P =
   offsetTable[block(P)] + bytes of marked granules between the block start
   and P (popcount over at most 8 bitmap words).
4. **Move-and-fix + sweep**: walk live objects in address order; for each,
   compute its destination, `memmove` it down (skip when dest == src), then
   rewrite the Value slots in the moved copy via the same per-type switch,
   computing referents' new addresses on demand. This fuses LISP2's separate
   update and slide passes: forwarding is pure bitmap/offset-table
   arithmetic on old addresses, so it stays computable even after a
   referent's old location has been overwritten (the very dependency that
   forces LISP2 to update before sliding). Also sweep `finalizable` here:
   free C-heap storage of dead entries, rewrite survivors' pointers (keyed
   on the mark bit instead of `forward`). Clear the bitmap (single memset).

Net cost: two touches of live data (mark, move-and-fix) plus an O(heap/64)
bitmap skim — the same touch count as Cheney's copy+scan, trading the
forward-pointer read for a load+popcount and paying for an explicit mark
stack where Cheney's scan doubles as its work queue.

Growth: steady-state collections compact in place with zero allocation. When
`alloc` still cannot satisfy the request after a collect, grow by compacting
into a fresh larger malloc (sliding into new space is the same walk), then
free the old space and reallocate the bitmap/offset table for the new size.
Same doubling policy and `maxBytes` cap as today; the "live > 50% => grow"
heuristic carries over.

## How the code changes

`src/heap.hpp`:

- `Obj` loses `forward`; header becomes { type u8, flags u8, pad u16,
  size u32, ident u32, pad u32 } = 16 bytes. (Keep ident in the object --
  `identityOf` stamping and stability across moves is unchanged.)
- `Heap` gains `markBitmap`, `offsetTable`, `markStack`, loses
  `toSpace/toSize/toUsed`. `copyObj`/`visitSlot` are replaced by
  `markSlot`/`updateSlot` plus `forwardingOf(Obj*)`.
- The root-walker contract changes: walkers are now invoked **twice** per
  collection (mark pass, update pass) and must visit the same slots both
  times. Update the comment on `addRoots`; audit existing walkers (State
  stack, namespaces, intern table, VM frames) -- they are all
  visit-every-slot loops today, so this should be a no-op, but each one gets
  eyeballed.
- Design-notes comment at the top of both files rewritten (they currently
  say "semispace Cheney scavenger", as does the header comment in
  `tests/test_substrate.cpp`).

`src/heap.cpp`:

- `alloc`: unchanged interface and bump path; the collect-then-grow loop
  stays, minus the to-space malloc failure mode. `OT_GC_STRESS` block stays
  as is.
- `collectInto` rewritten per the phases above. From-space poisoning under
  `OT_GC_STRESS` becomes poisoning of the tail gap (`used..spaceSize`) plus,
  during slide, poisoning of vacated ranges -- stale-pointer reads must still
  fail loudly.
- Destructor and `finalizable` sweep: same logic, keyed on mark bits.
- Constructors (`make_*_h`) and the tempRoots discipline are unchanged:
  compaction moves objects exactly like copying did, so every existing
  "root across alloc, re-derive pointers after" comment remains true.

Untouched by design: `value.hpp` accessors, all `as_*` functions, C-heap side
storage (array items, table entries, code bytes, Buf payloads -- none of it
moves), `table_get`/`table_put`/`array_push` alloc-free guarantees, foreign
object semantics (payloads still moved byte-for-byte, finalizers still must
not allocate), `heap_of`'s State-leading-Heap layout assumption.

## Instrumentation and A/B comparison

We are not maintaining two collectors side by side. Instead, sequence the
work so both collectors get measured with identical instrumentation:

- **Phase 0 (own commit, lands first)**: add GC stats to the *current*
  Cheney heap. The counters live in `Heap` next to the existing
  `collections` field and are cheap enough to be always-on:
  - memory: current `used`, `spaceSize`, live bytes after last collection,
    peak `used` high-water mark, and total footprint including side
    structures (to-space transient today; bitmap/offset-table/mark-stack
    after the switch) so the 2x-vs-flat story is visible in the numbers;
  - time: monotonic nanoseconds per collection (same clock as
    current-jiffy), accumulated total, max single pause, pause count.
  Expose as a `(gc-stats)` builtin returning a table, plus a `--gc-stats`
  CLI flag that prints a summary line to stderr at exit so the benchmark
  harness can capture it without modifying ports.
- **Phase 0b**: record the Cheney baseline on this host with the harness
  and check the numbers into the ticket (add-note): the GC-heavy r7rs set
  plus peak RSS. Tag the baseline commit (e.g. `gc-cheney-baseline`) so
  the A side stays one `git checkout` away for re-runs; that tag, not a
  build option, is the second collector.
- **Phase 1+**: the Compressor work proceeds as designed below, keeping
  the same stats fields and output format so every comparison is
  like-for-like.

GC-heavy benchmark runs (record before and after, same host, same flags):

    benchmarks/r7rs/run.py --impl ./build/otium gcbench destruc mperm deriv nqueens

gcbench/destruc/mperm are the designated allocation-and-mutation set from
lan-70gb; deriv and nqueens add list-churn coverage. Also run one
compute-bound control (fib or tak) to confirm the mutator path did not
move. Capture per-benchmark: wall time, reported time, collection count,
total GC ns, max pause ns, peak RSS (harness-side via getrusage or
/usr/bin/time -l), and peak heap footprint from the stats line. The
interesting deltas: total GC time and max pause (expected roughly flat to
slightly worse on low-survival workloads), peak footprint and RSS
(expected to drop by roughly the transient to-space), and collection
count (expected to drop from the 24->16 byte header).

## Keeping the GC well-tested

The stress hook is the backbone and it survives unchanged: `OT_GC_STRESS`
collects every Nth alloc (`OT_GC_STRESS_EVERY`, final gate at 1) and poisons
dead memory. Note the meson `gc_stress` option was dropped in b8e230f; the
stress build is now `-Dcpp_args=-DOT_GC_STRESS` (or a scratch meson setup).
Concrete plan:

1. **Substrate unit tests** (`tests/test_substrate.cpp`, runs against
   heap.cpp without State): keep every existing alloc/collect/identity/Buf
   test passing unmodified -- they encode the external contract. Add
   compaction-specific cases:
   - sliding preserves address order and coalesces free space (allocate A B C,
     drop B, collect, assert A and C are adjacent and ordered);
   - forwarding math at edges: object starting exactly on a block boundary,
     object spanning multiple blocks/bitmap words, live object at offset 0,
     heap with a single live object at the very end;
   - mark-stack overflow: build a deep list exceeding the bounded stack,
     collect, verify the rescan fallback traces everything (make the bound
     configurable or tiny under test);
   - growth path: fill past capacity, verify compact-into-larger-space keeps
     identities, finalizable entries, and payload bytes intact;
   - dead-object finalization: arrays/tables/buffers/foreign freed exactly
     once, survivors not finalized (existing tests cover some of this; extend
     for the bitmap-keyed sweep).
2. **Full suite under stress**: `otium-tests` plus the CLI tests with
   `OT_GC_STRESS` and `OT_GC_STRESS_EVERY=1`, and the prelude/expander
   bootstrap (the compiler's Slot/Scope discipline lists the GC-stress build
   as its gate -- lan-q4lf). This is the acceptance gate, same as the VM
   migration used.
3. **Sanitizers**: ASan+UBSan run of the full suite, with and without stress.
   A sliding memmove bug that overlaps wrongly or reads freed C-heap storage
   is exactly what ASan catches.
4. **Churn test**: a randomized object-graph exerciser in test_substrate --
   seeded PRNG builds/mutates/drops a graph of pairs, arrays, tables,
   strings, buffers over a few thousand iterations with a mirrored shadow
   structure in C++ land, verifying the shadow after every collect. Seeds
   logged on failure so runs are reproducible. This is the fragmentation /
   long-running proxy that single-shot unit tests miss.
5. **Benchmarks as regression evidence**: record the r7rs set (lan-70gb
   harness) before/after on the same host, at minimum gcbench, destruc,
   mperm (allocation-heavy) plus one compute-bound control. Also record peak
   RSS for gcbench before/after -- the entire point of the change is the
   memory ceiling, so measure it.
6. **Invariant checks**: an `OT_HEAP_VERIFY` debug walk (or fold into
   OT_GC_STRESS) that after each collection scans the heap and asserts every
   Value slot points at a valid in-bounds object header with a sane type --
   catches missed pointer updates immediately rather than three tests later.

## Acceptance criteria

1. Semispace machinery is gone: no to-space allocation anywhere; peak heap
   memory during collection is spaceSize plus bitmap/offset-table/mark-stack
   overhead, demonstrated by the gcbench RSS measurement.
2. `Obj` header is 16 bytes with a static_assert pinning it.
3. Full meson suite passes normally, under OT_GC_STRESS_EVERY=1, and under
   ASan+UBSan; prelude bootstrap and REPL smoke test pass under stress.
4. New substrate tests from the list above exist and pass, including
   mark-stack overflow and block-boundary forwarding cases.
5. Churn test runs in the normal suite (bounded iterations) and passes under
   stress.
6. GC stats (`(gc-stats)` builtin + `--gc-stats` flag) land as a separate
   commit on the Cheney collector first; the Cheney baseline for the
   GC-heavy benchmark set (with peak RSS and pause stats) is recorded in
   this ticket and the baseline commit is tagged before the collector is
   replaced.
7. r7rs before/after numbers compare identical instrumentation on both
   collectors; pause-time regression on allocation-heavy benchmarks is
   acknowledged and quantified, not silent.
8. Root-walker double-invocation contract documented in heap.hpp; all
   existing walkers audited and listed in the PR/commit message.
9. Design-notes comments in heap.hpp/heap.cpp/test_substrate.cpp no longer
   say "Cheney"/"scavenger" anywhere the algorithm is described.
