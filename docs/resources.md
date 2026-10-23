# Resource scopes

`with` and `close` are available in core on both runtimes.
`otium.resource` re-exports this protocol and supplies an owning wrapper for
other resources. Ports, owning C pointers, and SQLite handles all use the
same generic. See [the I/O design](../specs/io.md#with).

```scheme
(require otium.resource :as resource :refer (with close))
(require otium.ffi :as ffi)

(with ((memory (ffi/alloc 'u8 32) ffi/free!))
  (ffi/set! memory 'u8 0 42))
```

Bindings are sequential and visible to subsequent initializers. A binding
is `(name expression)` or `(name expression release-function)`. The latter
calls the supplied function with the resource as its only argument.
Without it, the resource must have an applicable `close` method for a normal
exit. This is checked before entering the body. A missing method is an error.

Resources are released in reverse acquisition order on normal return,
errors, restarts, interrupts, and quit. If acquisition fails, resources
already acquired are released. The body result is returned after cleanup.
Cleanup follows dynamic scope; re-entering a released scope via
a Chez continuation is unsupported.

`close` is a generic with two arguments: resource and outcome. The outcome
is `nil` on normal return or a restart transfer, the condition on error
unwinding, or `:quit` for quit and interrupts. A cleanup error replaces the
pending result, error, or control transfer. Outer resources are still released
if an inner resource's cleanup fails.

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

Ports dispatch as `Port` and owning FFI pointers as `CPointer`. Both have
`close` methods, so their `with` bindings need no explicit release function.
`(ffi/own pointer destroy)` gives a borrowed pointer an owning wrapper whose
destructor runs at most once. Views become invalid when the owner closes.
