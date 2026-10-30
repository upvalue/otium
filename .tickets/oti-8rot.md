---
id: oti-8rot
status: open
deps: []
links: []
created: 2026-10-30T04:18:18Z
type: task
priority: 2
assignee: Phil
tags: [ui, cleanup]
---
# ui.ot reaches otium.ui.window through eval

ui.ot:812 window-call runs (eval '(require otium.ui.window)) then evals a built symbol on every open/close!/run, to avoid a require cycle. Lint and the compiler can't see these calls, so a rename only fails at run time. Options: otium.ui.window registers its entry points in a defonce table in otium.ui when it loads (survives reload), or move open/run/close! into otium.ui.window and re-export.

