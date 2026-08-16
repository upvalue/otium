---
id: lan-rnxc
status: open
deps: []
links: []
created: 2026-08-16T00:36:57Z
type: bug
priority: 2
assignee: Phil
tags: [eval, ns]
---
# in-ns exists twice with divergent semantics

in-ns is both a special form in eval_tr (accepts quoted symbols/strings via unwrap_quote) and a native in sys.cpp (symbol only). The special form always wins in head position, so the native is only reachable higher-order (apply), where it behaves differently. Drop the native or make it delegate to the same path.

