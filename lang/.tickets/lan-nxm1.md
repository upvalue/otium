---
id: lan-nxm1
status: closed
deps: []
links: []
created: 2026-08-16T00:37:24Z
type: chore
priority: 3
assignee: Phil
tags: [cleanup, builtins]
---
# string.cpp charter: strings, symbols, keywords, buffers

builtins/string.cpp also holds symbol/keyword/name and the buffer natives. Either rename/re-charter the file's header comment to be honest about its contents, or move the symbol/buffer natives elsewhere. No new file required — smallest honest fix wins.

