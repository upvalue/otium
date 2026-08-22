# GSGC transplant

`GC=gsgc` is a native Otium collector based on Ian Piumarta's GSGC 1.0. The
runtime does not include `gsgc.h` or call the original public API. The
generation-scavenging algorithm lives in `src/ot-gc-gsgc.c` and uses the same
collector seam as `semi` and `gen`.

The algorithm has a copying new generation and a copying old generation. A
minor collection copies young survivors into the other new space. Objects move
to old space after `GSGC_MAX_AGE` minor collections. Stores from old objects to
young values add the owner to a remembered-object table. A full collection
copies reachable young and old objects into the other old space and leaves the
new space empty.

## Otium adaptations

GSGC 1.0 describes pointer fields with a machine-word bitmap. That does not fit
Otium's variable-sized slot arrays and table entries, so the transplant calls
`ot-gc-trace.inc` for exact roots and object fields. Tagged immediate values are
filtered by `ot_is_ptr`.

`ot_store` supplies the old-to-young barrier. Otium's extension side list stays
weak: each collection rebuilds it from forwarded objects and finalizes dead
extensions before their source space is reused. Space and remembered-table
storage goes through `ot_host_alloc`, and collection work is reported through
`ot_gc_stats`.

Otium can register the same root slot more than once. The original GSGC delays
rewriting roots until evacuation is complete; this transplant rewrites a slot
as it visits it. A destination-space check makes a repeated visit a no-op.

## Geometry and accounting

The initial new-space size is the larger of `HEAP_INIT` and
`GSGC_MIN_NEW_SPACE`. GSGC reserves two new spaces of that size and two old
spaces at twice that size, for six times the initial new-space size. Object
headers live inside those reservations. The remembered-object table and heap
structure count as metadata.

The old semispaces grow when the live set requires more room. `HEAP_MAX` rejects
a single Otium object larger than that value, but it is not a hard limit on the
collector's total reservation. `benchmarks/gc_compare.py` chooses `HEAP_INIT`
to match the requested initial collector-memory budget and records the actual
post-workload reservation.

The build-time policy knobs are `GSGC_MIN_NEW_SPACE`, `GSGC_MAX_AGE`, and
`GSGC_MAX_REMEMBERED` in `config.mk`.

Minor pauses use the `minor` statistics phase. Full generation-scavenging
collections use `full_copy`; the sweep and compact phases remain zero.

## Provenance

The transplant is based on GSGC 1.0, last edited by Ian Piumarta on
2011-09-16. The upstream permission notice is in `LICENSES/GSGC.txt`.
