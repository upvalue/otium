---
id: oti-ldd5
status: open
deps: []
links: []
created: 2026-10-29T06:37:13Z
type: bug
priority: 2
assignee: Phil
tags: [c, threads, dev]
---
# Find out why a sleeping thread never wakes under otium serve

A program with one thread spinning forever and one that sleeps 1 s then prints prints nothing in 3 s under `otium serve`. Run directly or with --dev it prints after 1 s. The serve loop sits in %poll 50 most of the time, which would slow other threads down but shouldn't stop the sleeper waking. See scratch/threaded-io.md, item 7.


## Notes

**2026-11-10T04:16:14Z**

scratch/threaded-io-notes.md (completion audit): looked into during the threaded dev server work and not reproduced. Retry once before spending time here.
