---
id: lan-2i27
status: open
deps: []
links: []
created: 2026-08-16T00:37:22Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup, strings]
---
# make_string overload taking a Buf

The pattern make_string(vm, out.data ? out.data : "", out.len) appears ~9 times across string.cpp/sys.cpp/reader.cpp. Add Value make_string(Vm&, const Buf&) and convert call sites.

