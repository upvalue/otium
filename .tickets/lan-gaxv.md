---
id: lan-gaxv
status: open
deps: []
links: []
created: 2026-10-21T15:35:44Z
type: task
priority: 3
assignee: Phil
tags: [ui, macos, manse]
---
# Show the app's name in the macOS app switcher when run from the interpreter

Run directly, Manse shows as "chez" in Cmd+Tab. configure-app! sets the process name through otium.objc (4830cc6), which fixes the menu, but LaunchServices (lsappinfo, NSRunningApplication localizedName) still reports "chez" and that's what the switcher shows. Phil remembers it working earlier; not reproduced, including with the old manse/application.ot set-name!. tools/ui-app.py gives a bundle name but Manse doesn't launch through it.


## Notes

**2026-10-28T00:22:59Z**

Chez runtime removed, so Manse now runs as the C otium binary. Check whether the switcher still shows the wrong name.
