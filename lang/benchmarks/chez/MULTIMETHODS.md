# Chez multimethod prototype benchmark

The first prototype resolves methods by scanning the registry on every call.
It implements exact type specializations and wildcard fallbacks without a
cache. The benchmark compares this with compiled `cond` dispatch using type
descriptors, map fields, and vector prefixes.

Run from `lang/`:

```sh
chez/test
chez/run examples/chez/multimethods/events.scm
benchmarks/chez/run-multimethods 200000
```

The runner accepts a call count and uses `$CHEZ`, defaulting to `chez`. It can
run from any working directory. `multimethods-results.csv` records the initial
run on October 16, 2026, with Chez 10.4.1, `tarm64osx`, and optimization level 0.
This uses the normal compiler settings of the prototype. It is a local
microbenchmark, not a GUI frame-time measurement.

## Workload

Each case constructs 1,024 input pairs using a deterministic PRNG. Payloads and
types vary. Inputs repeat through a timed loop, so the benchmark represents
warm, small working sets. One-specializer cases register 4, 16, or 64 exact
methods plus a fully unspecialized fallback. Two-specializer cases use 2x2,
4x4, or 8x8 combinations, plus one partial fallback per widget type and a full
fallback. Both cases call functions with two arguments.

Every exact method adds two input payloads and a constant unique to its type
combination. All strategies must produce the same checksum. The timed paths
receive runtime objects through an indirect procedure call. Object creation,
method registration, source generation, and compilation happen before timing.

There are five strategies:

| Strategy | Work performed |
| --- | --- |
| `preselected-method` | Fetch the already selected method from an input-indexed vector and call it. This is a reference cost, including vector lookup and indirect call, rather than dispatch from types. |
| `multimethod-scan` | Call a saved generic, compute argument descriptors, select among exact and fallback signatures, and invoke the method. |
| `cond-descriptor` | Read descriptors, run a compiled `cond`, and execute the matching body. |
| `cond-map-field` | Read the `kind` fields from the same typed maps, run a compiled `cond`, and execute the matching body. |
| `cond-vector-prefix` | Read tags from vector slot 0 and payloads from slot 1. This changes storage as well as dispatch, so it is a broader performance target. |

The generic and preselected baseline use the exact same implementation closures.
The `cond` cases compile the same arithmetic and map-field body into each branch.
Exact methods are installed through the internal registration API for this
benchmark, avoiding different namespace lookup costs inside the method bodies.
The public declaration syntax is covered by the test suite and example. Calls
through a live namespace alias can have additional overhead not measured here.

The `cond` implementations cover all exact combinations present in the measured
inputs. They do not implement extensible registration, general wildcard
resolution, or ambiguity errors. Their fixed bodies are useful targets for
optimization, not interchangeable implementations of the full language feature.

Each strategy warms up with up to 10,000 calls, then runs five samples of 200,000
calls. Collection happens before each timed sample. Elapsed monotonic time
includes any collection during the loop. CSV output includes the median and
minimum nanoseconds per call. Strategies run in a fixed order; system load,
frequency changes, and compiler choices can affect comparisons.

Allocation uses the difference between Chez `statistics` snapshots, which
accounts for allocation even when collection occurs. A plain difference of
`bytes-allocated` would instead conflate allocation with reclaimed memory.
See [Chez timing and statistics](https://cisco.github.io/ChezScheme/csug10.0/system.html).
Snapshot and timing overhead contributes a small fixed number of bytes per
sample; the near-zero baseline figures are not literal per-call allocations.

## Initial results

Median nanoseconds per call, rounded to the nearest nanosecond:

| Specialized arguments | Exact methods | Preselected | Multimethod | Descriptor cond | Map-field cond | Vector-prefix cond |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 4 | 16 | 178 | 16 | 26 | 7 |
| 1 | 16 | 16 | 313 | 18 | 26 | 10 |
| 1 | 64 | 17 | 878 | 31 | 38 | 22 |
| 2 | 4 | 16 | 268 | 18 | 26 | 7 |
| 2 | 16 | 16 | 438 | 19 | 27 | 11 |
| 2 | 64 | 17 | 1095 | 34 | 41 | 24 |

For 16 exact methods on two specialized arguments, the scan takes about 438 ns
versus 27 ns for map-field `cond`, roughly 16 times as long. At 64 exact methods,
the corresponding figures are 1,095 ns and 41 ns, roughly 26 times as long.
These ratios include the common loop and payload work; they are not isolated
instruction costs for selection alone.

The scan allocates about 636 to 3,294 bytes per call across these cases. It builds
argument and type lists, applicable-method lists, and filtering closures. That
is a concrete optimization target, alongside repeated matching. Applicability
scans all methods; finding undominated candidates is quadratic in the number
of applicable methods in the worst case. These workloads have two or three
applicable methods, so registry size drives their growth.

This result does not show that typed-map field storage is inherently too slow:
the descriptor and map-field `cond` cases already use the same objects and
payload lookups as the generic. It shows the cost of this deliberately simple
resolver. A next experiment could remove per-candidate allocations, then compare
that with an invalidated-on-registration cache. Preserve the uncached results
and run the same semantic tests for each implementation.
