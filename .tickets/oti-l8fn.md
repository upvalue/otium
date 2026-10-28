---
id: oti-l8fn
status: open
deps: []
links: [oti-cd09]
created: 2026-10-27T03:30:53Z
type: bug
priority: 2
assignee: Phil
tags: [prototype, benchmarks]
---
# Make the five failing R7RS ports run on the prototype

With benchmarks/r7rs/run.py --runtime c prototype/otium-proto (default --c.heap-max of 64MB), five ports fail, before and after the VM optimization commits:

- paraffins, nboyer, sboyer, earley: managed heap exhausted
- ack: VM stack limit exceeded

Haven't checked whether their live data really exceeds 64MB or whether the collector's growth policy is at fault. ack is deeply recursive and probably needs a larger --stack or a bigger default. The manifest already raises heap_max for mperm and gcbench.

## Acceptance Criteria

Each port either runs and reports its result, or the manifest gives it the limits it needs with a note on why.


## Notes

**2026-10-28T00:30:31Z**

benchmarks/r7rs/run.py no longer takes --runtime; there is only the C runtime.
