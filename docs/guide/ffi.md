# ✨ Foreign functions

Otium's foreign function interface (FFI) lets you call exported C functions in
shared libraries without writing a C wrapper. Load a library, describe each
function's C signature, and call the resulting Otium procedure.

[otium.ffi](/reference/otium.ffi/) also provides opaque pointers and native
memory for calls where C expects a buffer, writes an output parameter, or
returns a resource handle.

## Runtime and build support

The desktop C runtime supports the FFI in both native-code and bytecode VM
execution. The normal Meson build requires libffi; there is no separate FFI
enable option. Foreign calls work with the native compiler disabled and in the
desktop amalgamated runtime.

The browser/WebAssembly build and the freestanding otos build do not support
foreign calls.

The FFI targets the native C ABI on macOS arm64 and Linux x86-64. Do not assume
that another platform's calling convention or data layouts are compatible.

## Load a library and declare a function

These library names cover the system math library on macOS and Linux:

```otium
(require otium.ffi :as ffi)

(def math
  (ffi/open-library
    '("/usr/lib/libSystem.B.dylib" "libm.so.6" "libm.so")))

(def c-hypot
  (ffi/bind math "hypot" '(double double) 'double #f))

(c-hypot 3.0 4.0) ; => 5.0
```

[`open-library`](/reference/otium.ffi/#open-library) tries the names in order.
Paths with directory components are used directly; bare names follow the
operating system's library search rules. Libraries stay loaded for the process
lifetime.

[`bind`](/reference/otium.ffi/#bind) takes:

- The loaded library.
- The exact exported C symbol name.
- A list of argument types.
- The result type.
- A required `collect-safe?` boolean.

This declaration matches C's `double hypot(double, double)`. Use `#f` for a
short call whose behavior you know, and `#t` for blocking, long-running, or
uncertain calls. With either setting, foreign calls run synchronously and can
block Otium.

Binding resolves the symbol immediately. Otium does not read C headers or check
that your declaration matches them. A wrong signature can crash the process.

## Choose types from the C declaration

Scalar types are quoted symbols. Use the type corresponding to the library's
declaration:

- `int`, `uint`, `long`, `ulong`, `size-t`, and related names follow the
  target's C types.
- `i8`, `u8`, through `i64`, `u64` specify exact-width integers.
- `float` and `double` accept Otium integers or floating-point numbers.
- `bool` accepts only `#t` and `#f`.
- `void` is a result type and returns `#f`.
- `pointer` passes an address; `utf8` converts a NUL-terminated string.

Integer conversions check bounds rather than truncate or wrap. Native `i64` and
`u64` results must still fit Otium's integer range, −2⁶² through 2⁶²−1.

Calls have fixed arity. C varargs and C++ ABI calls are not supported.

## Pointers and output parameters

A foreign pointer represents an address, not an Otium array or integer.
`ffi/null` is the C null pointer. It is truthy, so test it with
`null-pointer?`, not `if` alone. Neither
`0`, `nil`, nor `#f` substitutes for it.

C's `modf` returns the fractional part of a number and writes the whole part
through a `double *`. Continuing the example:

```otium
(def c-modf
  (ffi/bind math "modf" '(double pointer) 'double #f))

(let ((whole (ffi/alloc 'double 1)))
  (defer
    (list (c-modf 12.75 whole)
          (ffi/ref whole 'double 0))
    (ffi/free! whole)))
; => (0.75 12.0)
```

[`alloc`](/reference/otium.ffi/#alloc) creates zeroed, aligned storage for a
count of elements. [`ref`](/reference/otium.ffi/#ref) reads an element;
[`set!`](/reference/otium.ffi/#set!) writes one. Their indexes are zero-based
element indexes, not byte offsets.

You must release native allocations explicitly with
`free!`; garbage collection does not release
them. `defer` runs the cleanup on normal return or unwinding; see [Errors and
recovery](errors.md).

A pointer returned by a C library has the lifetime and ownership specified by
that library. Call its documented destructor rather than passing an arbitrary
external pointer to `ffi/free!`. Keep allocation owners reachable while C uses
their addresses, including addresses stored in native memory.

Otium checks bounds and released state for its own allocations. It cannot stop C
from overrunning a buffer or prove that an external pointer is valid.

## Strings

Declare a read-only string argument as `utf8` when C uses it only during the
call. Otium supplies NUL-terminated UTF-8 bytes; embedded NUL characters are
rejected. A `utf8` result becomes an independent Otium string, or `ffi/null` for
a null result.

For strings C retains, use `pointer` with
`alloc-utf8`, and keep the allocation alive
until C is finished. For returned strings requiring a library destructor, use a
`pointer` result, copy with `read-utf8`, then
call that destructor. Automatic `utf8` conversion does not free the original
string.

## Structs and arrays

A layout describes a C struct or union's fields. Pass the layout value itself to
memory operations, not its quoted name.

```otium
(def Point
  (ffi/define-struct! 'Point '((x double) (y double))))

(let ((p (ffi/alloc Point 1)))
  (defer
    (begin
      (ffi/field-set! p Point '(x) 3.0)
      (ffi/field-set! p Point '(y) 4.0)
      (c-hypot (ffi/field-ref p Point '(x))
               (ffi/field-ref p Point '(y))))
    (ffi/free! p)))
; => 5.0
```

Layouts follow target C alignment and padding. Verify them against the library's
headers. Fixed arrays use an `(array T count)` type description.

In a function signature, a struct or union layout means by value; `pointer`
means an address. Build argument lists containing layouts with `list`, so the
layout values are evaluated. Aggregate results return newly allocated storage
that you must free.

## Handling failures

Loading, binding, and conversion failures raise conditions recognized by
[`ffi/error?`](/reference/otium.ffi/#error?). Use
[`error-reason`](/reference/otium.ffi/#error-reason) for a machine-readable
reason and `condition-message` for text.

C return values are not automatically treated as errors. Check each function's
documented convention, such as a null pointer or negative integer, and perform
any required cleanup yourself.

<!-- generated from ffi.md.spec sha256:2fb7b4c89959182efc373e3a1ea51c101b3abc4045224438b30817f5507ff4de -->
