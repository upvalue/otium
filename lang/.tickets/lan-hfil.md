---
id: lan-hfil
status: open
deps: []
links: [lan-qwff, lan-ius2, lan-ab4w]
created: 2026-08-16T20:28:42Z
type: feature
priority: 3
assignee: Phil
tags: [embedded, compiler, build]
---
# Ahead-of-time code image and a compiler-less build

Strawman, not a commitment. For a genuinely constrained deployment the cheapest way to cut allocation is to not do the work on the device at all.

state_create today compiles the embedded expander and prelude on every boot. That is the bulk of the 1525 allocations and 396 reallocs measured at construction, and it is identical work every single time. Serialise the post-bootstrap state to a code image on the host, mmap or link it in, and boot maps to loading it.

Combined with an OT_NO_COMPILER build that excludes compile.c, the reader and the expander, this deletes the compiler arena problem (lan-ius2) outright and most of the scratch buffer problem (lan-ab4w), because the remaining transient Buf sites are concentrated in the reader, the printer and error formatting.

Open questions, and they are the whole ticket really. What does the image format look like given a moving collector and interned symbol ids that are assigned in creation order. Whether eval and the REPL are simply absent in that build or whether there is a middle tier. Whether native module registration can still happen at runtime. And whether losing runtime macroexpansion is acceptable given how much of the prelude is macros -- it probably is, since they are all expanded at image build time, but the interactive story (lan-qwff) does not survive it.

Worth having the conversation before anyone builds anything.

## Acceptance Criteria

Design agreed and written down, or the ticket is closed as not worth it.

