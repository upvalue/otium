---
id: lan-qojh
status: open
deps: []
links: [lan-s095, lan-7l89, lan-7wkw, lan-6824]
created: 2026-08-22T04:39:30Z
type: task
priority: 1
assignee: Phil
parent: lan-70gb
tags: [gc, benchmarks, performance]
---
# Compare semi and gen with the GC-oriented R7RS ports

Extend the existing R7RS benchmark workflow to compare the semispace and generational collectors on gcbench, destruc, mperm, and takl. The direct C workloads are useful for isolated behavior; these ports add larger language-level allocation graphs and mutation patterns.

## Design

Run both collectors from fresh processes under the same reservation-plus-metadata budget. Consume the machine-readable GC statistics emitted by the runtime. Keep raw samples, warmup counts, runtime flags, collector geometry, host identity, Otium commit, and input identity. Report workload time, wall time, allocation throughput, GC percentage, inclusive maximum mutator pause, phase counts, peak object bytes, and collector memory totals. Do not compare results collected on different hosts as if they were paired.

## Acceptance Criteria

1. The R7RS runner can select semi or gen and records collector identity and geometry.
2. gcbench, destruc, mperm, and takl produce raw samples for both collectors under equal collector-memory budgets.
3. Results include the same pause and throughput fields as the direct C comparison.
4. At least five measured samples follow a documented warmup on arm64 and x86-64 hosts.
5. Incorrect results, crashes, and timeouts remain results rather than timings.
6. The benchmark documentation explains how to reproduce and interpret the comparison.
