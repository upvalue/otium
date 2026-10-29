---
id: oti-2ur2
status: open
deps: []
links: []
created: 2026-10-29T01:22:29Z
type: task
priority: 3
assignee: Phil
tags: [benchmarks]
---
# Run the upstream R7RS programs on Chez Scheme

benchmarks/r7rs/run.py can run the unmodified upstream programs on Guile (--reference guile), which isn't installed here. Chez 10.0.0 is installed as scheme, but the upstream sources import (scheme base) and friends, and ecraven runs Chez with --libdirs pointing at a private set of R7RS libraries. Plain Chez fails with 'library (scheme base) not found'.

Adding Chez needs an R7RS library layer for Chez (an existing one, or small shims mapping (scheme base) etc. onto (chezscheme)). define-record-type differs between R6RS and R7RS, so the shims aren't trivial. Then add chez to --reference and to benchmarks/extensive-report.py.

