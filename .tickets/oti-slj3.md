---
id: oti-slj3
status: closed
deps: []
links: []
created: 2026-11-15T23:08:16Z
type: task
priority: 2
assignee: Phil
parent: oti-fm19
tags: [gpu, ui]
---
# Split renderer.c into backend-neutral code and an SDL_GPU backend

Step 1 of scratch/webgpu-plan.md. Internal backend interface: create, resize, upload atlas rows, draw, present, capture, free. No behaviour change.


## Notes

**2026-11-15T23:13:11Z**

Split done, uncommitted: native/ui/renderer.h (backend interface), native/ui/gpu-sdl.c (SDL_GPU backend), renderer.c now backend-neutral. All 12 UI tests pass and all 22 test captures are byte-identical to before. manse pty/program/workspace/repl/browser ui fail on Linux before and after (they load /usr/lib/libSystem.B.dylib).
