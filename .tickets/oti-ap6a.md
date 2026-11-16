---
id: oti-ap6a
status: closed
deps: [oti-7o2o]
links: []
created: 2026-11-15T23:08:16Z
type: task
priority: 2
assignee: Phil
parent: oti-fm19
tags: [gpu]
---
# Otium bindings for webgpu.h

Step 4 of scratch/webgpu-plan.md.


## Notes

**2026-11-16T03:12:02Z**

Done: native/webgpu/generate.py writes lib/otium/webgpu/raw.ot; lib/otium/webgpu.ot builds descriptors from tables and wraps adapter/device requests, device errors and buffer readback. tests/webgpu/render.ot draws offscreen and reads back. Not done: creating a surface for an SDL window from Otium; see the follow-up ticket.
