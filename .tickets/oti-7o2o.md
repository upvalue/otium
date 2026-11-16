---
id: oti-7o2o
status: closed
deps: [oti-slj3]
links: []
created: 2026-11-15T23:08:16Z
type: task
priority: 2
assignee: Phil
parent: oti-fm19
tags: [gpu, ui]
---
# WebGPU spike: wgpu-native wrap and UI backend, measured against SDL_GPU

Step 3 of scratch/webgpu-plan.md. Measure binary size, start-up, memory and frame time on macOS arm64 and Linux.


## Notes

**2026-11-16T02:57:04Z**

Done on Linux x86-64 (Vulkan), commit c5eead1. Headless captures byte-identical to SDL_GPU; ui and manse suites pass with OTIUM_UI_DRIVER=webgpu. Measured against SDL_GPU: start-up 300 vs 340 ms (median of 5 one-frame headless runs), peak RSS 179 vs 167 MB, bench-paint 198x60 paint 1.38 vs 1.32 ms and submit 0.03 vs 0.01-0.03 ms. libwgpu_native.so 9.3 MB as shipped, 7.7 MB stripped (macOS dylib 6.9 MB unstripped). Not yet tried: direct rendering to a visible window (ui gpu direct with OTIUM_UI_VISIBLE=1), webgpu:gl, macOS.
