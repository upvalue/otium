---
id: on-lmue
status: open
deps: []
links: []
created: 2026-10-28T21:04:34Z
type: task
priority: 2
assignee: Phil
tags: [native, performance]
---
# Copy builtin arguments in dispatch a word at a time

In c/src/native.c dispatch, the builtin fast path copies native.args to the VM stack with memcpy (the line `if (argc) memcpy(rt->stack + base, rt->native.args, argc * sizeof(ot_value));`). The JIT has just written those arguments with 8-byte stores, and for two arguments glibc's memcpy reads them back with one 16-byte load. A load that spans two smaller stores can't be forwarded from the store buffer, so it waits for them to commit, about 20 cycles every call.

It shows up on any builtin that isn't compiled inline. In the R7RS primes benchmark, remainder goes through dispatch around 250M times, and perf puts 83% of samples on that memcpy (memmove-vec-unaligned-erms.S:343, called from dispatch). The same is true on d46b697 and f2ff283, so it was there before threads.

Fix: copy with a plain loop of 8-byte loads and stores for small argc (or always). The other memcpys of native.args in dispatch (the closure path) probably want the same.

Found while measuring segment crossings; see scratch/segmented-stacks.md.

## Acceptance Criteria

primes and other builtin-heavy benchmarks (deriv, wc, cat) measured before and after with perf stat cycles:u; the memcpy no longer dominates the primes profile.

