# Resource scopes

`otium.resource` provides the general `with`/`close` protocol from
[the I/O design](../specs/io.md#with), implemented in Otium on both runtimes.
It does not depend on ports or SQLite.

```scheme
(require otium.resource :as resource :refer (with close))
(require otium.ffi :as ffi)

(with ((memory (ffi/alloc 'u8 32) ffi/free!))
  (ffi/set! memory 'u8 0 42))
```

Bindings are sequential and visible to subsequent initializers. A binding
is `(name expression)` or `(name expression release-function)`. The latter
calls the supplied function with the resource as its only argument.
Without it, the resource must have an applicable primary `close` method
with an unrestricted outcome argument. This is checked before entering the
body. A missing method is an error.

Resources are released in reverse acquisition order on normal return,
errors, restarts, interrupts, and quit. If acquisition fails, resources
already acquired are released. The body result is returned after cleanup.
The implementation uses `unwind-protect`; re-entering a released scope via
a Chez continuation is unsupported.

`close` is a generic with two arguments: resource and outcome. The outcome
is `nil` on normal return, the condition on error unwinding, or `:quit` for
other nonlocal exits, including restarts. A cleanup error replaces a normal
result. During unwinding, cleanup errors are discarded to preserve the
original error or control transfer. Outer resources are still released if
an inner resource's cleanup fails.

```scheme
(defclass Lock token (closed? :init #f))

(defmethod (close (lock Lock) outcome)
  (unless (lock :closed?)
    (set! (lock :closed?) #t)
    (release-lock (lock :token))))
```

Each resource implements its own idempotence and closed-resource checks.
`(resource/own value destroy)` supplies a small owning wrapper for resources
without a class. `(resource/value wrapper)` retrieves its value while open
and raises after close. Its close method marks it closed and calls
`destroy` once, including when destruction raises. Destroy functions must
therefore release ownership even if they report an error.

Native FFI pointers do not yet have a distinct dispatch class. Use the
explicit release binding above, or wrap them with `resource/own` and access
them through `resource/value`. The future `Port` and native owning-pointer
methods from the I/O design are not implemented by this module.
