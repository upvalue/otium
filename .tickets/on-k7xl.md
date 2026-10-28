---
id: on-k7xl
status: open
deps: []
links: []
created: 2026-10-28T21:24:03Z
type: task
priority: 3
assignee: Phil
tags: [native, performance]
---
# Try aligning JIT function entries to 64 bytes

Moving JIT functions 16 bytes changes call-heavy benchmarks by up to about 10% (frontend stalls only, same instructions and branch misses). With entries aligned to 64 bytes (sljit_emit_aligned_label with 6 instead of SLJIT_LABEL_ALIGN_16 at NS_FUNC in c/src/native.c), divrec went from 2.88s to 2.80s, and f2ff283 vs 74d4c8a became equal on divrec and ack. Measure it across the R7RS suite. Even if it isn't a net win, aligned builds make before/after comparisons less noisy.

