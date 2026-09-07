---
id: lan-qazk
status: closed
deps: []
links: []
created: 2026-09-06T22:39:19Z
type: task
priority: 3
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Define the GC statistics schema once

GC statistic fields and formatting are repeated in the human CLI renderer, JSON CLI renderer, and benchmark emitter, creating substantial formatting code and drift risk.

## Acceptance Criteria

One field schema or visitor drives the relevant renderers; existing JSON keys remain compatible or are intentionally versioned; tests cover every public ot_gc_stats field.

