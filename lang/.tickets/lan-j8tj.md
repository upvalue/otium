---
id: lan-j8tj
status: open
deps: []
links: [lan-7l89, lan-7wkw]
created: 2026-08-22T16:53:30Z
type: feature
priority: 2
assignee: Phil
tags: [gc, performance, heap]
---
# Evaluate an exact remembered-object set for gen

gen's remembered set is a card table with five side arrays sized for every possible old-space card at init. That metadata cost is the core of lan-7l89, and card scanning contributes to minor pause work. gsgc's barrier keeps an exact vector of old objects with young referents instead: no metadata proportional to old_max, and minors trace only true old-to-young owners.

The gsgc collector was removed in 38c0753; its remembered-object design is worth evaluating for gen before the idea goes cold. The 128 MiB budget comparison showed gsgc minors doing less scanning for the same workloads; not yet measured whether that survives contact with gen's in-place old space.

## Design

Keep gen's spaces, marking, sweep, and compaction. Replace the remembered/overflow card roles with a bounded owner vector plus a duplicate-suppression bit, either stolen from the header or kept as a side bit per object start. Needs a bounded overflow fallback: when the vector fills, degrade to scanning all old objects during the next minor rather than growing from the barrier. The starts table stays, since sweep, compaction, and validation use it independently. Compare minor pause, metadata bytes, and barrier cost against the card table on gc_compare.py and the lan-qojh R7RS ports before committing either way.

## Acceptance Criteria

1. A measured comparison of card table vs exact set on minor pause, metadata bytes, and mutator throughput at 64 MiB and one larger geometry.
2. Overflow fallback is bounded and correct under gc_stress with a deliberately tiny vector.
3. A written decision on the ticket: adopt, hybrid, or keep cards.

