---
id: lan-hdkq
status: open
deps: []
links: []
created: 2026-08-16T00:36:33Z
type: task
priority: 2
assignee: Phil
tags: [runtime, performance]
---
# Make table printing linear in table size

Table printing calls table_entry_at once for every live entry. table_entry_at starts scanning the entry array from index zero each time, so printing an n-entry table is O(n²), even when there are no tombstones. Preserve insertion-order output while walking the table storage once.

## Design

Expose a cursor or iterator-style table API that advances through insertion-order entries without restarting the scan. Keep tombstone handling inside the table implementation rather than duplicating its sentinel rules in the printer.

## Acceptance Criteria

Printed table output and insertion order are unchanged. Printing visits each stored entry at most once, apart from constant setup work. Tables containing tombstones print only live entries. Tests cover ordinary tables and delete/reinsert ordering.

