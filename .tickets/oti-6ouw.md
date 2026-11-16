---
id: oti-6ouw
status: open
deps: []
links: []
created: 2026-11-16T02:57:04Z
type: task
priority: 2
assignee: Phil
parent: oti-fm19
tags: [gpu, ui]
---
# Check the WebGPU backend on macOS, in direct mode and on GL

Follow-up to oti-7o2o. Run ui gpu direct (OTIUM_UI_VISIBLE=1) with OTIUM_UI_DRIVER=webgpu on Linux; run the ui and manse suites on macOS arm64 (Metal layer surface is untested); try OTIUM_UI_DRIVER=webgpu:gl.

