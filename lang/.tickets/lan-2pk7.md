---
id: lan-2pk7
status: open
deps: []
links: []
created: 2026-10-19T05:22:55Z
type: feature
priority: 2
assignee: Phil
tags: [ffi]
---
# FFI callbacks on the C runtime

Implement ffi/callback, ffi/free-callback! and ffi/callback-count on C to the contract in specs/ffi.md#callbacks, which Chez implements (lan-6xjn). C currently has stubs: %callback fails with reason unsupported after core.ot checks the signature, free-callback! fails with type, callback-count returns 0 (c/src/ot-ffi.c). Use libffi closures; the trampoline is C, so it can check pthread_self against the Otium thread and abort with a message before entering the runtime. Callbacks run during a foreign call, so a collection can now happen inside n_call: re-check that encoded arguments, aggregate copies and result storage stay outside the moving heap and that managed values used after the call are rooted and reacquired. Run make gc-lint. Tests: run shared/tests/ffi/callbacks.ot and the exit/thread checks in shared/tests/test_ffi.py on C too (currently Chez only), including under --stress.


## Notes

**2026-10-19T05:22:59Z**

Paths above are pre-layout-move; shared/tests/ffi becomes tests/ffi.
