---
id: lan-hbm0
status: open
deps: []
links: []
created: 2026-10-17T23:24:49Z
type: bug
priority: 2
assignee: Phil
tags: [repl, tests]
---
# REPL ignores Ctrl-D under test_repl.py

shared/tests/test_repl.py fails at its final close(): Ctrl-D on an empty prompt doesn't exit within 5s. Seen on main at f8938ab, all earlier REPL checks pass. A bare pty probe (no TIOCSWINSZ, no reply to the ESC[6n cursor query) also saw both otium and otium-chez ignore Ctrl-D, so the cursor-position query is a suspect, not verified. Not yet checked which runtime the test trips on first.


## Notes

**2026-10-28T00:23:00Z**

Chez runtime removed; only the C REPL is left to check.
