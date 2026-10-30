---
id: oti-v7hu
status: open
deps: []
links: []
created: 2026-10-30T02:11:57Z
type: task
priority: 3
assignee: Phil
tags: [manse, perf]
---
# Reduce Manse's allocation while idle

An idle Manse, with no terminal output and no frames drawn, still allocates. Not urgent, but it shows up as memory creeping in the system monitor and as GC work for nothing.

What we saw (2026-10-29, one Manse, two terminals, idle):

- Otium heap: in-use bytes climb about 40 KB/s with the frame count unchanged. Live data stays about 0.9 MB and collections bring it back, so it's garbage rather than a leak. About 130 collections in 3 minutes, ~19 ms total.
- Native malloc: MALLOC_MEDIUM and MALLOC_SMALL dirty grew about 8.5 MB over one minute (vmmap -summary, a minute apart). Only one sample, so not yet known to keep growing.
- GPU: IOAccelerator went 106 MB -> 21 MB in the same minute. That's drawables coming and going, and accounts for the big swings in the footprint, not steady growth.

Theories, none checked yet:

- The UI loop runs workspace/step! every poll (16 ms focused, 100 ms in the background) even with nothing to do. scripted! calls process/getenv "MANSE_FRAMES" and string->number on every iteration, which allocates a string each time.
- pty/reap! and each terminal's apps/step! (reading the PTY reader ring, parsing) may allocate per tick even when there's no output.
- Native growth could be SDL_ttf (per-cell shaping creates a TTF_Text for each new grapheme, and fonts opened at each size stay open until UI teardown), the PTY reader's batches, SDL's event queue, or the runtime's own native allocations, e.g. FFI buffers allocated and freed per call in otium.memory/footprint and manse.sysinfo/native-threads. Those last two only run while the system info popup is open.

## Acceptance Criteria

With Manse idle and no output: the Otium heap's in-use bytes stay flat between frames, and native malloc stays flat over ten minutes.

