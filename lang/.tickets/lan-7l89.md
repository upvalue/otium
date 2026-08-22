---
id: lan-7l89
status: open
deps: []
links: [lan-s095, lan-qojh, lan-7wkw]
created: 2026-08-22T04:39:20Z
type: feature
priority: 1
assignee: Phil
tags: [gc, heap, performance]
---
# Scale the host heap without paying maximum metadata cost up front

The host build still defaults to a 64 MiB logical heap. The generational collector reserves the maximum old-space range and allocates side metadata for every possible card when the state is created, even though old space becomes active in smaller chunks. Define a host-oriented sizing policy and make a larger maximum practical on laptops and servers.

## Design

Keep an explicit maximum for predictable failure and embedding control. Evaluate a 256 or 512 MiB host default, or a documented host-memory policy. Separate reserved address space, committed storage, active logical capacity, and metadata in both implementation and statistics. Grow or commit old-space side metadata with active chunks where practical. Preserve the existing build and CLI overrides.

## Acceptance Criteria

1. A documented host default or sizing policy replaces the inherited 64 MiB assumption.
2. Selecting a large heap maximum does not require all card metadata to become resident at state creation.
3. GC statistics distinguish maximum, active capacity, reservation, and metadata costs.
4. Startup and collector measurements cover at least 64, 256, and 512 MiB maxima on an arm64 host.
5. Both semi and gen retain explicit heap maximum overrides.
