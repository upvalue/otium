---
id: oti-hmbx
status: closed
deps: []
links: []
created: 2026-11-15T23:34:02Z
type: bug
priority: 2
assignee: Phil
tags: [build]
---
# GCC 16 -Werror=maybe-uninitialized in builtins.c and renderer.c

The default build (cc = GCC 16.2.1) fails: c/src/builtins.c:177 'operand' and native/ui/renderer.c add_glyph 'x'/'y' may be used uninitialized. Clang builds are fine.


## Notes

**2026-11-15T23:36:02Z**

Fixed: initialised operand (builtins.c) and x, y (renderer.c add_glyph); manse/native/pty.c defines _DEFAULT_SOURCE since only the cachy-dev build passed it.
