# Prototype builtins that could move to Otium

The prototype has about 300 builtins in C. Many of them are thin layers over
other builtins and could be written in Otium in `prototype/primitives.ot` or the
shared library. This would shrink the C runtime and follow the project rule of
extending the language before adding escape hatches.

This is a list of candidates, not a plan. Everything here was checked against
the prototype's current behaviour, but nothing has been moved yet. Performance
matters, so builtins on hot paths should be benchmarked before and after a move.

## Already dead

`lib/otium/core.ot` defines these itself, and its definitions replace the
builtins when core loads. Neither `expander.ot` nor `primitives.ot` uses them
before then, so the C versions never run in a normal session:

- `contains?`
- `condition-type`
- `condition-message`
- `error?`
- `digit-value`

`read-string` is also redefined in core, but core keeps the native version as
`%read-datum-string` and calls it for the string case. It should stay, ideally
under that name.

This was checked by running `(describe 'name)` for every builtin. Only these six
names come back with core's documentation.

## Direct rewrites

These map onto existing builtins with no loss:

- `exact?` and `inexact?` are `int?` and `float?` for every number the
  prototype has.
- `pos?` and `neg?`. `primitives.ot` already defines `positive?` and
  `negative?`.
- `nan?`, `infinite?` and `finite?`, by comparing a float with itself and with
  the infinities.
- `modulo`, over `remainder`.
- `close-input-port` and `close-output-port`, over `close-port` plus a direction
  check.
- `print`, `println`, `display`, `write` and `newline`, over `display-to`,
  `write-to` and the `current-output-port` param that `primitives.ot` already
  defines. `print` separates its arguments with spaces, and `println` adds a
  newline.
- `get-output-bytevector` or `get-output-string`: one can be written over the
  other with `utf8->string` or `string->utf8`.
- `open-binary-input-file` and `open-binary-output-file` differ from the text
  versions only in a flag. One native open that takes a mode would cover all
  four.
- `find-restart`, over `compute-restarts` and `restart-name`.
- `values` on tables, over `keys` and `get`.
- `char-upcase`, `char-downcase`, `char-whitespace?`, `char-alphabetic?` and
  `char-numeric?`. These are ASCII-only, so `char->integer` and range checks
  are enough.
- `list?`, as a tortoise and hare walk over `pair?` and `cdr`.

## Possible, with a cost to watch

- `min`, `max` and `abs`. Note that `(min 1 2.0)` is `1.0`, so the Otium version
  has to apply float contagion. These turn up in numeric loops, so benchmark
  them.
- `string->list`. A loop over `string-ref` would be quadratic, because strings
  are UTF-8. Reading characters from a string port, or adding a native
  next-character primitive, avoids that.
- `str`, over a string port and `display-to`.
- `read-line`, `read-all`, `read-until`, `read-while` and `skip-while`, over
  `peek-char`, `read-char` and a buffer. `read-while` and `skip-while` already
  call back into the VM for every character, so they lose least. `read-all`
  on large files would be slower than the native copy.

## Should stay native

Arithmetic and comparison (the compiler inlines several of these), `equal?` and
hashing, table and array storage, the reader, number formatting and parsing,
UTF-8 encoding, port buffering, the filesystem, process and time builtins, and
the FFI.

The FFI exposes both `ffi/X` and `ffi/%X` names for several operations
(`sizeof`, `alignof`, `bind`, `open`, `alloc`, `ref`, `set!`). Some of the
unprefixed ones may only exist for legacy names that `lib/otium/ffi.ot` could
provide. That needs a closer look at `ffi.ot` before anything is removed.
