---
id: oti-ns3j
status: open
deps: []
links: []
created: 2026-11-15T23:39:28Z
type: bug
priority: 2
assignee: Phil
tags: [ui, build]
---
# ui gpu test: calculator Tab focus fails on the GCC build

With the default (GCC 16) build, the calculator step of 'ui gpu' fails: after Tab, (ui/focused tree) is nil (error: nil is not callable at window.ot:361). The clang build passes. Deterministic. Findings: events pushed by tests/ui/events.c and key names delivered by otui_poll_event are identical in both builds. Swapping libraries: clang runtime + GCC libotium-ui-test-events.so fails ('Native calculator keys failed'); GCC runtime + clang events lib passes; GCC runtime + clang libotium-ui.so fails. Bool returns go through libffi uint8, which zero-extends, so that is probably not it. Next: compare frame timing of the pushed events vs step!, and check the GCC events lib's codegen.

