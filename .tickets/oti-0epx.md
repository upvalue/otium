---
id: oti-0epx
status: closed
deps: [oti-ap6a, oti-d5d0]
links: []
created: 2026-11-15T23:08:16Z
type: task
priority: 3
assignee: Phil
parent: oti-fm19
tags: [gpu]
---
# Move the FPS demo plan to WebGPU

Step 5 of scratch/webgpu-plan.md. Rewrite scratch/fps-demo-plan.md to use the WebGPU bindings.


## Notes

**2026-11-16T03:14:21Z**

Rewrote scratch/fps-demo-plan.md for otium.webgpu: step 1 now starts from the generated bindings and tests/webgpu/render.ot, WGSL shaders reload on every platform, the struct-layout and Linux open questions are gone (both answered by the generator), field-set! numbers updated for add14f3.
