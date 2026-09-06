---
id: lan-qojh
status: in_progress
deps: []
links: []
created: 2026-08-22T04:39:30Z
type: task
priority: 1
assignee: Phil
parent: lan-70gb
tags: [gc, benchmarks, performance]
---
# Compare semi and gen with GC-oriented language benchmarks

Bring the gcbench, destruc, mperm, and takl ports into Otium's native benchmark directory and compare the semispace and generational collectors on them. The direct C workloads are useful for isolated behavior; these programs add larger language-level allocation graphs and mutation patterns without depending on the upstream R7RS runner.

## Design

Run both collectors from fresh processes under the same reservation-plus-metadata budget. Use Otium's benchmark harness to reset and consume machine-readable GC statistics around each language workload. Keep raw samples, warmup counts, runtime flags, collector geometry, host identity, Otium commit, and input identity. Report workload time, wall time, allocation throughput, GC percentage, inclusive maximum mutator pause, phase counts, peak object bytes, and collector memory totals. Do not compare results collected on different hosts as if they were paired.

## Acceptance Criteria

1. The native GC comparison can select semi or gen and records collector identity and geometry.
2. gcbench, destruc, mperm, and takl produce raw samples for both collectors under equal collector-memory budgets.
3. Results include the same pause and throughput fields as the direct C comparison.
4. At least five measured samples follow a documented warmup on arm64 and x86-64 hosts.
5. Incorrect results, crashes, and timeouts remain results rather than timings.
6. The benchmark documentation explains how to reproduce and interpret the comparison.

## Notes

**2026-08-22T17:54:14Z**

The four language GC workloads now live in benchmarks/ and run through gc_compare.py without the R7RS runner. Five measured x86-64 samples after one warmup passed for both collectors at a 128 MiB reservation-plus-metadata budget. Arm64 samples are still outstanding.
