---
id: lan-qpni
status: closed
deps: []
links: []
created: 2026-10-17T05:16:05Z
type: task
priority: 2
assignee: Phil
parent: lan-xfrj
tags: [portability, docs]
---
# Write the shared-code rules

Document what shared libraries use: parens, quoted symbols, lists, #f, typed maps, named let; and what they avoid: table/array literals, keywords, nil, relying on a missing get, let with dependent bindings until that is settled.

## Acceptance Criteria

Doc in docs/ listing the subset.


## Notes

**2026-10-17T21:45:13Z**

docs/portability.md records the shared API, module rules, reload behavior, and intentional differences. GAPS.txt records remaining limitations.
