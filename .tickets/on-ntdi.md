---
id: on-ntdi
status: open
deps: []
links: []
created: 2026-10-28T21:24:03Z
type: bug
priority: 2
assignee: Phil
tags: [c, signals]
---
# Close the race between a second SIGINT and read(2) in the server

c server interrupt (c/tests/tests.py, server_interrupt) fails about 10% of runs under ASan. A second SIGINT sent right after the paused prompt lands between oti_interrupt_point and the read syscall in ports.c transfer, and the server then blocks in read(0). Seen at 74d4c8a and d4e6207; f2ff283 passed 20/20, which isn't conclusive. Likely fix: a self-pipe that the handler writes, polled together with stdin.

