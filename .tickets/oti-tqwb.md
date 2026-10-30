---
id: oti-tqwb
status: open
deps: []
links: []
created: 2026-10-30T04:06:35Z
type: task
priority: 2
assignee: Phil
tags: [perf, runtime]
---
# substring scans the whole string and copies twice

c/src/support.c:389-417: the UTF-8 walk runs to the end of the string even after start and end are found, then the bytes are copied twice (once through malloc, then into the heap string). Taking a short prefix of a large string is O(length); parsers that slice repeatedly go quadratic. Stop the walk at end and copy once. The dedupe agent may touch the double copy (shared string-copy helpers).

