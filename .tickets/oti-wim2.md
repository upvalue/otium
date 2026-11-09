---
id: oti-wim2
status: open
deps: []
links: [oti-ycik]
created: 2026-11-02T15:15:40Z
type: bug
priority: 1
assignee: Phil
tags: [ui, manse]
---
# A window whose frames keep failing can't quit

Every way out goes through the failing frame:
- Manse's :quit? is acted on in after-frame!, which runs at the end of frame!, inside the run loop's try.
- Cmd+Q, the close button and SIGTERM arrive as SDL quit events. SDL turns SIGTERM into a quit event by default and nothing opts out. They're handled in the event loop behind the same failing sync!.
- Ctrl+C raises an interrupt inside the frame, and run's catch treats it as a frame error.
Only kill -9 stopped a stuck browser-ui test.

Proposed: handle quit/close before the view; run after-frame! outside the frame's try; let interrupts through the loop's catch; exit immediately on a second SIGTERM/Ctrl+C within a couple of seconds; stop retrying a frame that fails with the same error until state changes.

