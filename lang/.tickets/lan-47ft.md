---
id: lan-47ft
status: open
deps: []
links: []
created: 2026-08-20T02:55:25Z
type: chore
priority: 2
assignee: Phil
tags: [vm, concurrency, naming]
---
# Rename VM continuation records to resume records

Rename the process-owned VM suspension machinery from continuation terminology to resume terminology. Scheme already gives continuation a specific language-level meaning, while these records are internal opcode phase/state records used to resume a yielded VM process. Prefer concise names such as ot_vm_resume, resumes, vm_resume_dynamic, and vm_free_resume.

## Design

Use resume/resume record consistently for the internal C types, fields, helpers, GC labels, and tests. Do not change Scheme semantics or introduce a user-visible continuation feature.

## Acceptance Criteria

No internal process-suspension type or helper uses continuation as its primary name; the replacement terminology is shorter and clearly distinct from Scheme continuations; make test and tools/format-c --check pass.
