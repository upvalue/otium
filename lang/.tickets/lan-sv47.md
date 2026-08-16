---
id: lan-sv47
status: open
deps: []
links: []
created: 2026-08-16T00:37:22Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup]
---
# Deduplicate small byte/UTF-8 helpers

utf8_count defined identically in heap.cpp:217 and builtins/data.cpp:350; is_ws in reader.cpp:12 and builtins/string.cpp:77; string.cpp's sbytes duplicates printer.cpp's static string_bytes (and heap.hpp's inline string_bytes). One shared home (corner of common.hpp or a small header) ends the drift risk.

