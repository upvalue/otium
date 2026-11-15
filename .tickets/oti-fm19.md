---
id: oti-fm19
status: open
deps: []
links: []
created: 2026-11-15T23:08:16Z
type: epic
priority: 2
assignee: Phil
tags: [gpu, ui]
---
# Move GPU work to WebGPU; keep a CPU backend for the UI

Plan in scratch/webgpu-plan.md. WebGPU (wgpu-native on desktop, browser WebGPU on the web) for 3D and GPU work; the UI keeps its own draw list with GPU and CPU backends so it can run on microcontrollers with a framebuffer.

