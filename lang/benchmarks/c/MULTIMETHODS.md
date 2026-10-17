# C multimethod trial

Run from `lang/`, using a binary built with the intended collector:

```sh
make -C c GC=gen WITH_RAY=0
python3 benchmarks/c/multimethods.py c/bin/arm64-darwin/otium --calls 30000
```

Change the binary path for your platform. The checked-in CSV was collected on
October 16, 2026 with the generational collector, Apple arm64, and the default
`-O2` build. Other development work was running on the machine; these are
exploratory measurements, not a controlled performance study.

The runner compares a generic with preselected methods, descriptor-based
`cond`, and map-field-based `cond`. Each has the same three-argument wrapper
(index, left object, right object) around a two-argument operation. Inputs use
128 deterministically mixed object pairs with varying numeric payloads. Every
method body adds both payloads and a constant identifying the selected pair.
Checksums must agree. The preselected baseline includes a vector lookup and
an indirect call, but no runtime type selection.

One-specializer cases have 4, 16, or 64 exact methods and a fully unspecialized
fallback. Two-specializer cases have 2x2, 4x4, or 8x8 exact combinations, a
fallback per left type, and a fully unspecialized fallback. All measured inputs
hit exact methods. The fixed `cond` implementations cover those combinations;
they do not implement general wildcard ambiguity detection or registration.

Each strategy warms up for 2,000 calls and reports the median of five samples
of 30,000 calls. Timing uses `current-jiffy` and includes the loop, argument
lookup, method execution, and any collection. Object construction and method
registration are excluded. Strategy order is fixed. No empty-loop subtraction
or allocation measurement is performed.

Two-specializer results, median nanoseconds per call:

| Exact methods | Preselected | Native generic | Descriptor cond | Map-field cond |
| --- | ---: | ---: | ---: | ---: |
| 4 | 574 | 614 | 891 | 916 |
| 16 | 584 | 737 | 1204 | 1185 |
| 64 | 616 | 1029 | 2158 | 2315 |

The generic is faster than these `cond` implementations in these cases because
selection runs in native C, while the conditionals execute in the bytecode VM.
The preselected path remains faster. This does not establish that multimethods
inherently outperform conditionals, nor that the C runtime outperforms Chez:
the Chez experiment has different compilation, namespace access, input, and
loop costs. The shared target is predictable semantics and a reproducible
comparison within each runtime.

The resolver uses the same exact/wildcard specificity relation as Chez. It
allocates no managed objects on successful selection, but still rescans the
registry. Caching and specialized lookup structures remain future work.
