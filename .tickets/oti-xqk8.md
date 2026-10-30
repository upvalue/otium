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


## Notes

**2026-10-30T05:24:35Z**

Also: ffi/error? hides core's error? inside ffi.ot, the same trap ffi/null? was (now ffi/null-pointer?). %fail-at's and %as-operation's handler-binds rely on the ffi meaning. Allowed for now with a lint: allow(shadowed-core-name) at ffi.ot:244. Decide whether to rename it (e.g. ffi/ffi-error?); callers catch with (ffi/error? problem) in memory.ot, manse/sysinfo.ot and the ffi tests.
