---
id: lan-3ok7
status: closed
deps: []
links: []
created: 2026-10-17T05:16:07Z
type: task
priority: 2
assignee: Phil
parent: lan-xfrj
tags: [portability]
---
# Provide defonce and dotimes on both runtimes

C has dotimes but no defonce; Chez has defonce but no dotimes (and do is Chez-only). defonce is needed for state that survives reload.

## Acceptance Criteria

Both forms exist with the same behaviour on both runtimes, with tests.


## Notes

**2026-10-17T21:45:13Z**

defonce and dotimes run on both runtimes. Shared expander, library, and module reload tests cover preservation of false values and saved state.
