---
id: oti-g4kr
status: open
deps: []
links: []
created: 2026-10-29T01:22:29Z
type: bug
priority: 2
assignee: Phil
tags: [benchmarks, gc, memory]
---
# Find out why the earley port exhausts a 1 GiB heap

benchmarks/r7rs/ports/earley.ot crashes with 'managed heap exhausted' on the canonical input after about 11.7 s, with --c.heap-max=1073741824 (1 GiB per semispace, 2.1 GB peak RSS). The reduced input in r7rs/tests.py passes.

Several Schemes also crash or get killed on earley in ecraven's published results, so the input may genuinely need a lot of memory. It may also be the port or the runtime keeping parse state alive that it shouldn't. Not investigated yet.

To reproduce: python3 benchmarks/r7rs/run.py --only earley

