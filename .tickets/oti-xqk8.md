---
id: oti-xqk8
status: open
deps: []
links: []
created: 2026-10-30T04:18:18Z
type: task
priority: 2
assignee: Phil
tags: [ffi, cleanup]
---
# FFI error accessors should be plain Otium

ffi.ot:251-275: error?, error-reason etc. call %error-field (foreign.c:1030), which mallocs ':<field>', interns it and does (get c key #f). Replace with (get c :reason #f) etc. in Otium and delete the builtin.

