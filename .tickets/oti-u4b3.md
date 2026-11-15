---
id: oti-u4b3
status: open
deps: []
links: []
created: 2026-11-15T23:34:02Z
type: bug
priority: 2
assignee: Phil
tags: [sdl, linux]
---
# SDL binding tests fail on Linux: no x86_64-linux manifest

native/sdl/manifests has only arm64-darwin.json, so all eight sdl tests fail on x86_64 Linux reading the manifest. native/sdl/setup.py generates one.

