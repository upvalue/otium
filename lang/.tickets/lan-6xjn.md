---
id: lan-6xjn
status: open
deps: []
links: []
created: 2026-10-19T04:34:28Z
type: feature
priority: 1
assignee: Phil
tags: [ffi, manse]
---
# FFI callbacks on Chez (synchronous, same-thread)

Allow C to call Otium procedures during a foreign call. Needed by manse (libghostty-vt effects: write_pty, size, device_attributes, title_changed, bell, color_scheme). Chez first; C runtime via libffi closures is a follow-up ticket and may lag (ffi/callback fails as unsupported on C until then). Update specs/ffi.md 'Later callback work' to specify the contract below, and delete stale statements there and in docs/ffi-implementation.md that say there are no callbacks.

## Design

Agreed with Phil 2026-10-18:
1. Scope: synchronous, same-thread only. Callback may run only while Otium is inside a foreign call it made, on the Otium thread. Invocation on any other thread aborts the process with a message.
2. API: (ffi/callback arg-types result-type proc) -> value that counts as a pointer (passes as pointer or callback-pointer). (ffi/free-callback! cb) releases; unfreed callbacks live for the process. Freeing while C holds it is undefined; document it. ffi/callback-count for leak checks.
3. Reload: proc captured by value; document the forwarding idiom (fn args (apply ns/handler args)).
4. Errors and non-local exits inside a callback are caught at the boundary; callback returns zero/false/null for its result type; the first captured condition is re-raised when the outermost foreign call returns to Otium. handler-bind handlers still run at the signal site inside the callback.
5. Types v1: scalars, pointer, callback-pointer for args and result. No utf8, no by-value aggregates (rejected at ffi/callback time). Callbacks may make nested foreign calls.
6. GC: a collection can now occur during a foreign call. Chez utf-8 argument bytevectors may move; copy utf8 args to malloc'd temporaries (reuse %bind's temporaries list). Measure the cost with the FFI benchmark before deciding whether to restrict it. Verify the C runtime already keeps call temporaries out of the moving heap; do not change C beyond that check.
Implementation hint: Chez foreign-callable + lock-object, as in chez/otium/line-edit.sls.

## Acceptance Criteria

New fixture functions in the shared FFI fixture library: call a callback N times; store a callback and call it later; callback that makes a nested foreign call. Tests in shared/tests/ffi on Chez cover: forced collection inside a callback while a utf8 argument is live; error inside callback re-raised after the call; callback following reload via forwarding; callback-count after free; rejection of utf8/aggregate types in callback signatures. Existing FFI and SDL tests still pass (make test-ffi, make test-chez).

