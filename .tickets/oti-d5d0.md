---
id: oti-d5d0
status: open
deps: []
links: []
created: 2026-11-16T03:12:02Z
type: task
priority: 2
assignee: Phil
parent: oti-fm19
tags: [gpu]
---
# Create WebGPU surfaces for SDL windows from otium.webgpu

The FPS demo needs a surface: Wayland or X11 handles from SDL window properties on Linux, a Metal layer from SDL_Metal_CreateView on macOS, then configure, get-current-texture and present. gpu-webgpu.c's make_surface does the same in C.

