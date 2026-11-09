# ✨ Foreign functions

Use `otium.ffi` to load a shared library and call its exported C functions from
Otium. You need the C declarations: Otium does not read headers or check that
your signature matches the library. A wrong signature can crash the process.

## Load a library and call a function

This example calls the system math library on macOS or Linux:

```scheme
(require otium.ffi :as ffi)

(def math-library
  (ffi/open-library
    '("/usr/lib/libSystem.B.dylib" "libm.so.6" "libm.so")))

(def native-hypot
  (ffi/bind math-library "hypot" '(double double) 'double #f))

(native-hypot 3.0 4.0) ; => 5.0
```

`open-library` tries the names in order. Use a path for a specific library; bare
names follow the operating system's library search rules. Loaded libraries stay
resident.

For the C declaration `double hypot(double x, double y)`, `bind` takes:

- The loaded library.
- The exact exported name, `"hypot"`.
- A list of argument types, `'(double double)`.
- The result type, `'double`.
- A required collection-safety boolean.

Use `#f` for short calls whose behavior you know. Use `#t` for blocking or
long-running calls, or when their threading behavior is uncertain. Neither
setting makes a call asynchronous: blocking C code blocks the Otium execution
thread.

`bind` returns a callable procedure and reports missing symbols or invalid
signatures immediately. Use an empty list, `'()`, for a function with no
arguments.

## Choose types from the C declaration

Scalar types are quoted symbols.

| C value | FFI type |
| --- | --- |
| Fixed-width integers | `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, `u64` |
| Native C integers | `short`, `ushort`, `int`, `uint`, `long`, `ulong`, `size-t`, `ptrdiff-t` |
| Floating-point numbers | `float`, `double` |
| C `_Bool` | `bool` |
| Data address | `pointer` |
| NUL-terminated UTF-8 string | `utf8` |
| No result | `void` |

Integer arguments must be exact integers within the declared type's range. They
do not wrap or truncate. Even with `i64` or `u64`, results and memory reads must
fit Otium's integer range, `[-2^62, 2^62-1]`.

`float` and `double` accept integers or floating-point numbers. `bool` accepts
only `#t` and `#f`. A `void` result becomes `#f`.

Use fixed-arity C functions. Variadic calls such as `printf` and C++ ABI calls
are unsupported.

## Pass an output pointer

C functions often write results through a pointer. Allocate native storage, pass
it, read the value, and free the storage.

The C function `double modf(double x, double *integral)` returns the fractional
part and writes the integral part:

```scheme
(def native-modf
  (ffi/bind math-library "modf" '(double pointer) 'double #f))

(let ((integral (ffi/alloc 'double 1)))
  (unwind-protect
    (let ((fraction (native-modf 12.75 integral)))
      (list fraction (ffi/ref integral 'double 0)))
    (ffi/free! integral)))
; => (0.75 12.0)
```

`alloc` takes an element type and count and returns zeroed, aligned storage.
`ref` reads an element at a zero-based index. To write one, use
`(ffi/set! pointer type index value)`.

`unwind-protect` runs cleanup on both normal return and errors. Native
allocations need explicit release; garbage collection does not free them.

Pointers do not carry an element type. Supply the type on each read or write.
Otium checks bounds, alignment, and whether owned storage has been released. A
`view` shares its owner's storage; freeing the owner invalidates its views too.

For a pointer returned by C, follow that library's ownership rules. Otium cannot
establish its size or lifetime. Call the library's destructor when required; do
not pass an arbitrary C allocation to `ffi/free!`.

### Check for null

Use `ffi/null` for a null pointer argument. Neither `0`, `nil`, nor `#f` is a
substitute.

Test pointer results with `ffi/null-pointer?`. The value `ffi/null` is truthy,
so an ordinary `if` does not detect a null result.

The FFI does not turn a C function's null, negative, or false result into an
error. Check the function's documented failure convention yourself.

## Pass strings

Declare a `utf8` argument when C reads a NUL-terminated string only during the
call. Otium supplies temporary, read-only bytes. Embedded NUL characters are
rejected.

A `utf8` result becomes an independent Otium string; a null result becomes
`ffi/null`. This conversion does not free the C string.

If C retains or modifies the string, use a `pointer` argument and explicit
storage from `ffi/alloc-utf8`. Keep it alive until C finishes using it, then
call `ffi/free!`.

For returned strings that require a destructor, declare a `pointer` result. Copy
the text with `ffi/read-utf8`, supplying a maximum byte count that includes room
for the terminating NUL, then call the native destructor under cleanup
protection.

## Work with structs

Describe fields in C declaration order:

```scheme
(def Pair
  (ffi/define-struct! 'Pair '((tag u8) (value double))))

(let ((pair (ffi/alloc Pair 1)))
  (unwind-protect
    (begin
      (ffi/field-set! pair Pair '(value) 3.5)
      (ffi/field-ref pair Pair '(value)))
    (ffi/free! pair)))
; => 3.5
```

Pass the layout value `Pair`, not the symbol `'Pair`. Otium calculates native
alignment and padding. Check that the declaration matches the library's headers.

Use `pointer` in a function signature for a pointer to a struct. For a struct
passed by value, put the layout value directly in the argument list, building it
with `list`. A layout used as the result type returns fresh owning storage that
you must free.

`define-union!` describes unions. Fixed array fields use `(array T count)`;
field paths can include array indices. Standalone arrays are passed to C through
pointers.

## Runtime support

The hosted C runtime supports the FFI in both native-compiled execution and
bytecode VM mode (`--c.vm`). It requires libffi. The supported desktop targets
are macOS arm64 and Linux x86-64; the freestanding runtime does not provide this
FFI.

Callbacks into Otium are restricted to synchronous calls on the Otium thread
during a foreign call. Do not register an Otium callback with an API that
invokes it from a worker thread.

For handling loading and conversion failures, use `ffi/error?` with Otium's
[error handling forms](errors.md). `ffi/error-reason` identifies causes such as
`load`, `symbol`, `type`, and `range`.

<!-- generated from ffi.md.spec sha256:4371b0051cc54ad24c32d44c9eb90c5aa9a80f3740629cc1699da5f75185435c -->
