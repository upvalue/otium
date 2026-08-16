---
id: lan-f05r
status: open
deps: []
links: []
created: 2026-08-16T15:36:21Z
type: task
priority: 3
assignee: Phil
tags: [printer, code-quality]
---
# handle cyclic structures in print_val and val_equal

print_val (printer.cpp:99) and val_equal recurse on pairs/arrays with no depth or cycle guard; set-car! exists (data.cpp:332), so printing or comparing a cyclic list hangs or overflows the C stack. Table keys are protected (freeze_pair_key); plain printing/equality are not. Deliberately deferred for now — when picked up, a depth counter is the cheap fix; full cycle detection (visited set or Brent/Floyd) is the complete one.

