---
id: lan-b9xd
status: open
deps: []
links: []
created: 2026-10-21T14:56:23Z
type: feature
priority: 2
assignee: Phil
tags: [ui, dev-server, manse]
---
# Inject input events into a running UI through the dev server

Live development can't drive a running UI with real keyboard input. Checking which key name SDL reports for Cmd+Shift+? in Manse needed either OS-level event posting (CGEventPostToPid, which needs Accessibility permission) or asking Phil to press keys. Calling input/keyboard! from eval skips the native path (SDL key naming, the text-input event that follows a keydown, menu key equivalents), which is where the questions were. Want: a dev-server command that pushes synthetic SDL key/text/pointer events into a window's queue (e.g. SDL_PushEvent) so they go through native/poll-event like real input, plus a way to see the events the window received.

