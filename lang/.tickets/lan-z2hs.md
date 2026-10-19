---
id: lan-z2hs
status: open
deps: []
links: []
created: 2026-10-19T04:47:33Z
type: task
priority: 1
assignee: Phil
tags: [ui, manse, design]
---
# Strawman: event-driven ui/run and multi-PTY I/O thread for manse

STRAWMAN FOR DISCUSSION. Do not implement until Phil has agreed the design. ui/run currently sleeps a fixed 16 ms per iteration and repaints every window every frame. manse needs low latency, idle at zero CPU, and many terminals in one process.

## Design

Loop (shared/lib/otium/ui/window.ot run):
- Wait in SDL_WaitEventTimeout instead of sdl/delay 16. Timeout = the earliest of the next animation deadline, a dev-server poll interval, or none.
- Wake sources: SDL events, a user event posted by native threads (PTY data, dev socket), timers.
- A window repaints only when marked dirty (input, step!, a reload, a resize or a widget request). Otherwise the frame is skipped.
- Widgets may draw incrementally. The terminal widget redraws only the rows libghostty marks dirty, which needs per-row GPU data kept between frames rather than re-recorded each frame.

I/O thread (native, one per process, not one per terminal):
- poll() over every PTY fd plus a control pipe (add/remove fd, shutdown).
- Per PTY: an input ring buffer filled by the thread. When it is full, the thread stops polling that fd so the kernel buffer fills and that child blocks. Other PTYs are unaffected.
- Per PTY: an output queue. Otium (including write_pty callbacks) appends; the thread writes when the fd is writable. Otium never makes a blocking write().
- Posts a single SDL user event when any buffer goes from empty to non-empty.
- Child exit: SIGCHLD or waitpid(WNOHANG) is reported as an event per PTY.

Otium side, per wake:
- For each terminal with data, parse at most N bytes (round-robin), call render-state update and mark the window dirty. If bytes remain, request another wake immediately.
- Parsing, effects callbacks and drawing all stay on the Otium thread (matches the lan-6xjn callback contract).

Open questions:
1. Does the I/O thread also serve the dev-server socket, replacing the dev transport's own poll?
2. Is the native boundary ring buffers exposed as views, plus an explicit consume/commit? Or copy into Otium byte buffers?
3. Spawning: posix_spawn plus variadic-argument FFI for ioctl from Otium, or spawn and resize inside the I/O helper?
4. Byte budget N and the per-frame time limit: fixed constants, or tuned against vsync?
Future: parsing on native threads per terminal would need locking around libghostty state and a different callback contract. Keep each PTY behind its own object so that stays possible.

