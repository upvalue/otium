# GSGC collector implementation

Otium's `gsgc` collector is a single-threaded, stop-the-world collector with a
copying young generation and a copying old generation. Young objects carry an
age and move to old space after surviving the configured number of minor
collections. A remembered-object set records old objects that may point back
into the young generation.

The algorithm is based on Ian Piumarta's GSGC 1.0. Otium implements it through
the native collector boundary in `src/ot-gc-gsgc.c`: roots and object fields use
Otium's exact tracer, heap writes use `ot_store`, extensions use Otium's weak
finalization path, and collection work is reported through `ot_gc_stats`.

## Source layout

- `src/ot-gc-gsgc.c` implements spaces, allocation, evacuation, promotion, the
  remembered set, weak extensions, validation, and statistics.
- `src/ot-gc-common.c` owns root-frame bookkeeping, extension finalizer
  dispatch, phase counters, and statistics reset.
- `src/ot-gc-trace.inc` is the shared description of roots and object pointer
  fields.
- `src/ot-gc-internal.inc` defines the common object header, collector
  boundary, and `ot_store`.
- `src/otium.c` contains the mutator and routes heap-pointer writes through
  `ot_store`.
- `src/otium.h` exposes collector configuration and statistics while keeping
  collector state opaque.

The build selects this implementation with `GC=gsgc`. Exactly one of `semi`,
`gen`, or `gsgc` is linked into a runtime.

## Heap layout

The collector owns four bump-allocated spaces:

```text
young generation: [ new space ][ young survivor space ]
old generation:   [ old space ][ old survivor space ]
```

`new_space` and `old_space` are active between collections. Their survivor
spaces are evacuation destinations. A minor collection swaps the two young
spaces. A full collection copies every reachable object into old survivor
space, empties young space, and swaps the two old spaces.

The initial young-space size is the larger of `HEAP_INIT` and
`GSGC_MIN_NEW_SPACE`, rounded to host alignment. Each old space starts at twice
that size. The initial reservation is therefore six times the selected young
space size:

```text
2 * young size + 2 * (2 * young size) = 6 * young size
```

Old survivor space grows when the current live set plus one young-space reserve
does not fit. Young space can also grow for a single object too large for its
current capacity. Space growth allocates a replacement region; it happens only
when the old contents are empty or about to be replaced by a full collection.

`HEAP_MAX` limits the size of one Otium object in this collector. It is not a
hard limit on the four-space reservation.

## Object layout

Each allocation has one GSGC-private machine word immediately before the
ordinary Otium object:

```text
┌─────────────────────┬─────────────────────────────────┐
│ GSGC state word     │ Otium header, fields, and data  │
└─────────────────────┴─────────────────────────────────┘
                      ^ object pointers point here
```

The Otium header stores the aligned object size and type. GSGC reads that size
to find the next block, so its private word does not duplicate size
information. On a 64-bit target the private overhead is eight bytes per
object.

The private word is a compact state value:

| State | Meaning |
| ---: | --- |
| `0` through `GSGC_MAX_AGE` | Young-object age |
| `GSGC_MAX_AGE + 1` | Old object without a known young referent |
| `GSGC_MAX_AGE + 2` | Remembered old object |

With the default maximum age of four, the complete state range is zero through
six. A state above `GSGC_MAX_AGE` identifies an old object.

Blocks and object sizes use Otium's eight-byte alignment. A 16-byte Otium
object consumes 24 bytes in a GSGC space, and a 40-byte object consumes 48
bytes.

During evacuation, the collector reads the source size, copies the private word
and object, then replaces the source Otium header with the destination pointer
and sets bit zero. Any later reference to the same source object resolves
through that tagged forwarding pointer. The destination retains the normal
Otium header copied before the source was overwritten.

All heap pointers name exact object starts. Interior pointers are not
supported.

## Exact tracing

`src/ot-gc-trace.inc` describes every strong pointer field in every Otium
object. GSGC instantiates it with `gsgc_trace_slot`, producing direct walkers
for objects, VM state, and roots.

Precise roots include native `OT_FRAME` and `OT_GLOBAL` slots, state-owned
language values, the VM value stack and bytecode frames, condition machinery,
restarts, and dynamic parameter frames. Tagged immediates are rejected by
`ot_is_ptr` before any heap work.

A trace slot handles three cases:

1. A pointer already names the current evacuation destination, so no work is
   needed. This also makes repeated registration of the same root slot safe.
2. The source object has already moved, so the slot is rewritten from its
   forwarding pointer.
3. The source object has not moved, so it is copied or promoted and the slot is
   rewritten to the new address.

Minor tracing leaves old objects in place. Full tracing evacuates both
generations into old survivor space.

## Allocation

`ot_alloc` rounds the requested Otium object size to eight bytes and adds the
private state word. New objects use state zero and bump through active new
space. The full block is cleared before the normal Otium header is installed.

Allocation requests a collection when the block does not fit or the remembered
set exceeds `GSGC_MAX_REMEMBERED`. It first runs minor collections, which age
young survivors and can reduce the remembered set. A full collection follows
when new space still cannot satisfy the request or old survivor space lacks the
headroom required for promotion and the next full copy.

After a full collection, a large request may grow new space to twice its block
size. If old headroom is still short, old survivor space grows to the active
old bytes plus one complete young space, then another full collection installs
the larger old space.

Setting `gc_stress` requests a minor collection before each allocation while
young objects are present.

## Write barrier and remembered objects

Every heap-pointer mutation goes through:

```c
ot_store(state, owner, slot, value);
```

The store happens first. In a `gsgc` build, the barrier ignores immediate
values and stores from young owners. An old owner receiving a young pointer is
added to the remembered-object vector and changes to the remembered-old state.
The state prevents duplicate vector entries.

The remembered set stores exact owner pointers rather than cards. A minor
collection traces the complete pointer layout of every remembered owner. An
owner remains in the vector when tracing encounters a young referent. This is
conservative when that referent promotes during the same scan; the next minor
collection removes the owner if no young edge remains. Otherwise the owner
returns to the ordinary old state.

Promoted objects enter the remembered set before their fields are scanned. This
ensures references from a newly old object to other young survivors participate
in the same minor collection.

## Minor collection

A minor collection first checks whether old space has enough free bytes for a
worst-case promotion of active young space. If it does not, the collector runs
a full collection instead. Young survivor space grows to match new space when
needed.

The minor collection then proceeds as follows:

1. Reset young survivor space and trace all roots.
2. Copy a reachable young object into survivor space when its age is below
   `GSGC_MAX_AGE`, then increment its age.
3. Copy an object at the maximum age into old space and add it to the
   remembered set.
4. Trace remembered old objects and retain only those that still reach the new
   young destination.
5. Cheney-scan objects appended to young survivor space. Promotions discovered
   during this scan add more remembered objects, so remembered and survivor
   scanning continue until neither produces work.
6. Rebuild the weak extension list and finalize unreachable young extensions.
7. Swap new and young survivor spaces, then update logical young and total
   usage.

The discarded young source space is reused by the next collection.

## Full collection

A full collection places every reachable object in old survivor space. Before
tracing, it ensures the destination is larger than the physical bytes currently
used by both active spaces. When growth is required, it adds one
minimum-young-space reserve to that upper bound.

The collection clears the remembered set, traces roots, and Cheney-scans old
survivor space until all reachable fields have been processed. Every copied
object receives the ordinary old state. Young objects copied by this operation
count as promoted.

After weak extensions are processed, new space is reset and the old spaces are
swapped. The previous old space becomes the next full-copy destination. The
collector grows that destination if it is smaller than the newly active old
space. No young objects remain after a full collection.

An explicit `ot_collect` always requests this full collection.

## Weak extensions and finalization

`state->exts` is a weak side list and is absent from the strong root walker.
After evacuation, the collector walks its source list:

- an old extension remains in place during a minor collection;
- an evacuated extension is relinked through its forwarding pointer;
- an extension without a forwarding pointer is unreachable and is finalized
  before its source space can be reused.

The rebuilt list contains destination pointers only. Otium's shared extension
finalizer dispatch ensures each dead extension is released once.

## Validation

Defining `OT_GC_VALIDATE` adds an exact heap-validation pass. When `gc_stress`
is active, validation runs after each collection. It checks that roots and
object fields point to exact starts in active new or old space, object sizes are
aligned and remain within space bounds, and private states agree with the
generation containing the object.

## Statistics and accounting

Minor pauses update the `minor` phase. Full collections update `full_copy`.
Both also update the aggregate mutator-pause phase. The mark-sweep and compact
phases remain zero for this collector.

`used_bytes`, `allocated_bytes`, `copied_bytes`, and `promoted_bytes` count
aligned Otium object bytes. They exclude the private state word. The state word
still consumes space and is included indirectly in collection frequency and
the space reservation.

`capacity_bytes` is the current active new-space size plus active old-space
size. `reserved_bytes` covers all four spaces. `metadata_bytes` covers the heap
structure and remembered-object vector. `largest_free_region_bytes` is the
larger remaining bump region in active new or old space. GSGC does not report
internal fragmentation because both active generations are contiguous bump
spaces.

## Configuration

The collector-specific build settings live in `config.mk`:

| Setting | Default | Meaning |
| --- | ---: | --- |
| `GSGC_MIN_NEW_SPACE` | 1 MiB | Minimum size of each young semispace |
| `GSGC_MAX_AGE` | 4 | Young collections survived before promotion |
| `GSGC_MAX_REMEMBERED` | 1,024 | Remembered-set pressure threshold and initial capacity |
| `GC_TIMING` | 1 | Record collection pause time |

`benchmarks/gc_compare.py` chooses `HEAP_INIT` so GSGC's initial reservation
fits the requested comparison budget. Since old survivor space can grow, the
benchmark records the actual post-workload reservation for each sample.

## Provenance

The generation-scavenging design comes from GSGC 1.0 by Ian Piumarta. Otium
uses its space organization, age-based promotion, remembered-object barrier,
and full-copy structure. Otium supplies its own object representation, exact
root and field tracing, weak extension handling, host allocation, validation,
and statistics. The runtime does not include GSGC headers or expose its API.

The project README credits Ian Piumarta and records GSGC as the source of this
collector.
