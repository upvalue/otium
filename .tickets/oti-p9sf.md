---
id: oti-p9sf
status: open
deps: []
links: []
created: 2026-10-29T01:22:29Z
type: task
priority: 2
assignee: Phil
tags: [gc, memory, benchmarks]
---
# Check why large heaps grow straight to heap-max

Peak RSS for R7RS ports at canonical sizes (native, 2026-10-29, measured with benchmarks/maxrss.c):

- paraffins 2.10 GB and nboyer 2.10 GB with --c.heap-max=1 GiB. Both crash with 256 MiB, so they need somewhere between 256 MiB and 1 GiB of semispace.
- gcbench 1.05 GB with 1 GiB, mperm 4.2 GB with 2 GiB.
- Most other ports stay under about 10 MB.

Peak RSS equal to twice heap-max suggests that once a program needs a large heap, growth goes all the way to the cap rather than to some multiple of the live data. Measure the live size after the last collection for paraffins and nboyer to tell whether this is the growth policy or real live data.

To reproduce: python3 benchmarks/r7rs/run.py --only paraffins nboyer

