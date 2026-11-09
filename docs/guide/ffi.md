# ✨ Foreign functions

Otium's foreign function interface (FFI) lets you call exported C functions in
shared libraries without writing a C wrapper.

With the `otium.ffi` namespace, you load a library and declare a function's C
signature to get an ordinary callable Otium value. Supply the signature from the
library's headers or documentation; Otium does not read C headers for you.

## Runtime and build support

The desktop C runtime supports the FFI in both native-compiled code and the
bytecode VM (`--c.vm`). The standard Meson build requires libffi and builds the
native FFI helper, with no separate FFI enable flag. From a checkout,
`just build` builds these together.

The supported FFI targets are macOS arm64 and Linux x86-64. WebAssembly builds,
including browser and Node builds, do not support the FFI.

## Load a library and declare a function

This example calls the system math library's `hypot`, whose C signature is
`double hypot(double, double)`:

```otium
(require otium.ffi :as ffi)

(def math
  (ffi/open-library
    '("/usr/lib/libSystem.B.dylib" "libm.so.6" "libm.so")))

(def c-hypot
  (ffi/bind math "hypot" '(double double) 'double #f))

(c-hypot 3.0 4.0) ; => 5.0
```

`open-library` tries its candidate names in order. Paths name a library
directly; bare names use the operating system's library search rules. Loaded
libraries remain resident for the process lifetime.

`bind` takes the library, exact exported symbol name, argument-type list, result
type, and a required `collect-safe?` boolean. It resolves the symbol immediately
and returns a callable.

Use `#f` for short, known operations such as these math calls. Use `#t` for
blocking or long-running calls, or when their threading behavior is uncertain.
Neither setting makes the call asynchronous: native calls block the calling
Otium thread.

A wrong declaration can crash the process. Match the C signature exactly,
including whether an argument is a value or a pointer.

## Types at the boundary

Scalar types are quoted symbols:

- C integer types include `int`, `uint`, `long`, `ulong`, `size-t`, and
  `ptrdiff-t`. Fixed-width types include `i8`, `u8`, through `i64`, `u64`.
- `float` and `double` accept Otium integers or floating-point numbers.
- `bool` means C `_Bool` and accepts only `#t` or `#f`.
- `void` is a result type; such calls return `#f`.
- `pointer` means a native data address.
- `utf8` converts a NUL-terminated C string.

Integer conversions check bounds rather than wrapping or truncating. Even `i64`
and `u64` results must fit Otium's integer range, −2^62 through 2^62−1.

Calls have fixed arity. Variadic functions and the C++ ABI are not supported.

## Pointers and output parameters

A foreign pointer is an opaque address, not an integer. Use `ffi/null` for a
null pointer, not zero, `nil`, or `#f`. It is truthy, so test it with
`ffi/null-pointer?`.

C functions often write additional results through pointers. For example,
`double modf(double, double *)` returns a fractional part and writes the
integral part into caller-provided memory.

Continuing with `math`:

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

`alloc` creates zeroed native storage for the requested number of elements.
`ref` reads an element; `set!` writes one. Both take a type and a zero-based
element index. Pointers themselves do not carry an element type.

Release allocations explicitly with `free!`; garbage collection does not release
them. [`defer`](/reference/otium.core/#defer) runs the cleanup both on normal
return and when an error unwinds the expression.

Pointers returned by C have no inferred owner or bounds. Follow the library's
lifetime rules and use its destructor for resources it creates. Do not pass an
arbitrary library-owned pointer to `free!`. If C retains an allocated pointer,
keep its Otium owner reachable and wait until C has finished using it before
freeing it.

## Strings

A `utf8` argument accepts an Otium string or `ffi/null`. Its bytes are read-only
and valid only during the call; embedded NUL characters are rejected. A `utf8`
result is copied into an Otium string, but its native source is not freed.

For a string that C retains, declare a `pointer` argument and allocate it with
`ffi/alloc-utf8`. Free it only after native use ends.

For a returned string requiring a library destructor, declare a `pointer`
result. Copy it with `ffi/read-utf8`, supplying a maximum byte count, then call
the destructor under cleanup protection.

## Structs and unions

A layout describes native fields and alignment. `define-struct!` and
`define-union!` return layout values; bind those values with `def`.

```otium
(def Point
  (ffi/define-struct! 'Point '((x double) (y double))))

(let ((p (ffi/alloc Point 1)))
  (defer
    (begin
      (ffi/field-set! p Point '(x) 3.0)
      (ffi/field-ref p Point '(x)))
    (ffi/free! p)))
; => 3.0
```

Pass the layout value, `Point`, rather than the symbol `'Point`. Verify layouts
against the library's C declarations; packed structs and bitfields are
unsupported.

In a call signature, a layout means an aggregate passed by value. Use `pointer`
for a pointer-to-struct parameter. Build argument lists containing layouts with
`list`, rather than quoting the layout names. Aggregate results return newly
allocated storage that you must free.

## Handling failures

Loading, binding, and conversion failures raise conditions recognized by
`ffi/error?`. Use `ffi/error-reason` to distinguish causes and
[`condition-message`](/reference/otium.core/#condition-message) for readable
diagnostics.

The FFI does not interpret a library's return values as errors. Check null
pointers, status codes, and other failure indicators according to that library's
documentation. Memory checks cannot protect you from an incorrect signature or a
C function writing past a buffer.

<!-- generated from ffi.md.spec sha256:2fb7b4c89959182efc373e3a1ea51c101b3abc4045224438b30817f5507ff4de -->
