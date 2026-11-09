---
id: oti-01od
status: open
deps: []
links: []
created: 2026-11-02T01:33:44Z
type: bug
priority: 2
assignee: Phil
---
# Partly off-screen Manse column doesn't repaint new terminal output

With a column scrolled partly off the left edge of the strip, new output in its terminal reaches Ghostty (g/text shows it) but the window keeps showing the old picture. Focusing the column so it's fully visible paints it correctly. Seen with the Development column's REPL; also on main before the pty-programs work, so it's in painting, not the PTY path.

Likely in otium.ui.gpu's recorded-subtree replay (prepare-paint! / paint-node! with clip), or the terminal's gfx/cached outer cache when its bounds are clipped.

Repro: a workspace scripted! that opens Development then Echo (Echo focused, Development partly off screen), sends the REPL a line after a few ticks, and captures a second later.

