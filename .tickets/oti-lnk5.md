---
id: oti-lnk5
status: open
deps: []
links: []
created: 2026-10-25T03:04:50Z
type: bug
priority: 2
assignee: Phil
tags: [c, runtime]
---
# C aborts with 'root frame pop out of order' when the prelude fails to load

Symptom: if a prelude form raises, the C runtime aborts at startup with "otium: root frame pop out of order" instead of reporting the error. Repro: ./otium --runtime=c --c.max-depth=50 any.ot. The prelude needs more than 50 VM frames, so it raises "maximum VM frame depth exceeded", and C aborts. HEAD did the same before otium.loop existed.

Where to look:
- ot_create (c/src/otium.c) evaluates the prelude with eval_source and calls ot_destroy when that fails. The abort happens before that returns, or during ot_destroy. Not checked which.
- Embedded modules are initialised by void functions (extra_module_init, loop_module_init) that ignore what eval_source returns. A module that fails while it is required from the prelude, as otium.loop is, leaves the require running in a half-unwound state.

Seen while moving for into otium.loop: test_named_let in c/tests/test_runtime.c ran with max_depth 64, and loading otium.loop from the end of core.ot needs about 85 frames. That test now uses 256.


## Notes

**2026-10-28T00:30:31Z**

The launcher no longer takes --runtime; the repro is now ./otium --c.max-depth=50 any.ot.
