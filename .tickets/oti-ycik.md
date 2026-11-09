---
id: oti-ycik
status: open
deps: []
links: [oti-wim2]
created: 2026-11-02T15:15:39Z
type: bug
priority: 1
assignee: Phil
tags: [ui, manse]
---
# A failing view drops every input event, locking the UI

frame! in lib/otium/ui/window.ot rebuilds the view (sync!) before handling each event. If the view throws, the event is lost, and so is every one after it, Escape included, so the UI can't get out of the bad state. Seen with the Manse palette's bad :padding on a label (fixed in 397247c); only the dev server could recover it. The error is raised mid-reconcile, so the tree can be left half-updated.

Proposed, in order:
- A window-level keymap, checked when a key arrives without needing the view. Manse moves its workspace bindings there (Cmd+P, Cmd+W, Escape, column keys). Widget keymaps still run first when the view is healthy.
- If a rebuild fails, the event still goes to the window keymap instead of being lost.
- Manse wraps each column's and popup's view and step! in a try and draws an error card, so one broken app doesn't take down the rest.
- Check widget options when descriptions are made (in the view function), not mid-reconcile.
- Paint a small error strip over the last good frame while frames fail.

