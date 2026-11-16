---
id: oti-wdpp
status: open
deps: []
links: []
created: 2026-11-16T03:25:44Z
type: bug
priority: 1
assignee: Phil
tags: [language, reader]
---
# [...] and {...} read as plain (array ...) and (table ...) lists, so names can hijack them

The reader turns [x] into (array x) and {k v} into (table k v), indistinguishable from lists the user wrote. Two consequences: (1) a local named table or array turns literals into calls of the local (L0205 now warns); (2) worse, (def (g array) body) defines g with NO parameters, and (def (h array x) ...) one parameter: the expander takes any parameter list headed by the symbol array for [..] bracket syntax (expander.ot exp-head-is? params 'array). Proposed fix: the reader emits head symbols user code can't shadow or write by accident (say %array and %table, bound to array and table), the printer shows them as [..] and {..}, and every place that recognises the bare heads (core.ot match patterns, expander.ot params, loop.ot, dev.ot decoding, sqlite.ot, compiler/lower.ot and types.ot, ffi.ot's (array T n), thread.ot) checks the new heads. Needs a decision on the spelling.

