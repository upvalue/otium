# ✨ Values and types

Otium's type predicates distinguish missing data, empty collections, and
ordinary values. Its equality tests compare numbers by value, data by contents,
or references by object identity.

Much will be familiar from Scheme, but `nil` is separate from the empty list,
there are fewer numeric types, and arrays compare structurally with `equal?`.

## Recognizing values

[`type`](/reference/otium.core/#type) returns a keyword describing a value.
Predicates return `#t` or `#f`.

```otium
(type [1 2])                                ; => :array
(list (number? 3.0) (int? 3.0) (float? 3.0)) ; => (#t #f #t)
```

The everyday value types are:

| Values | Predicate | Meaning |
|---|---|---|
| `nil` | `nil?` | Absence or a missing result |
| `'()` | `null?` | The empty list |
| `#t`, `#f` | `boolean?` | Booleans |
| `42`, `-7` | `int?` | Bounded integers |
| `3.5`, `3.0` | `float?` | Double-precision floating-point numbers |
| `'name` | `symbol?` | Symbolic names |
| `:name` | `keyword?` | Self-evaluating tags and keys |
| `#\a` | `char?` | Unicode characters |
| `"hello"` | `string?` | Immutable Unicode strings |
| `'(1 . 2)` | `pair?` | Mutable pairs, also used to build lists |
| `[1 2]` | `array?` | Mutable, growable arrays |
| `{:a 1}` | `table?` | Mutable, insertion-ordered maps |
| `#u8(1 2)` | `bytevector?` | Mutable, fixed-length byte sequences |

`number?` accepts both integers and floats. `list?` recognizes proper lists,
including the empty list; a nonempty proper list is also a pair.

Other built-in values have more specialized roles:

- Buffers are mutable string builders, recognized by `buffer?`.
- Functions are recognized by `procedure?`, and macros by `macro?`.
- Dynamic params hold dynamically scoped settings and have type `:param`.
- Restarts represent recovery points and have type `:restart`.
- Ports represent input or output streams, recognized by `port?`,
  `input-port?`, and `output-port?`.
- The end-of-file value is recognized by `eof-object?`.

See [Data structures](data-structures.md) for collections and [Strings, symbols
and keywords](strings.md) for text and names.

## False values and the empty list

Only `nil` and `#f` are false in a conditional. Everything else is true,
including the empty list, zero, empty strings, empty arrays, and empty tables.

`nil` means absence. The empty list `'()` is a separate value with type `:null`.
Use [`nil?`](/reference/otium.core/#nil?) to test for absence and
[`null?`](/reference/otium.core/#null?) to test for the end of a list.

```otium
(list (nil? nil) (null? nil) (null? '()) (nil? '()))
; => (#t #f #t #f)

(list (if nil :yes :no)
      (if #f :yes :no)
      (if '() :yes :no)
      (if 0 :yes :no)
      (if "" :yes :no))
; => (:no :no :yes :yes :yes)
```

In Scheme, only `#f` is false; Otium also treats `nil` as false. Neither
equality test treats `nil`, `'()`, and `#f` as interchangeable.

`not` tests falsiness. `false?` tests specifically for the boolean `#f`, so it
does not recognize `nil` as a boolean.

A missing table lookup returns `nil`. Storing `nil` in a table removes the key;
use `#f` to store a false boolean value.

## Numbers

Otium has integers and floats, with no rationals or bignums.

Integers range from −2⁶² through 2⁶²−1. Floats use IEEE 754 double precision, so
fractional arithmetic can involve rounding. `int?` and `float?` test the
representation: `3.0` is a float even though its value is a whole number.

Division returns an integer when dividing integers exactly. A fractional result
becomes a float, rather than a Scheme rational. A float operand produces a float
result.

```otium
(/ 6 2)   ; => 3
(/ 7 2)   ; => 3.5
(/ 6 2.0) ; => 3.0
```

Use `quotient` for integer division truncated toward zero.

## Choosing an equality test

### Numeric equality with `=`

Use `=` to compare numbers. It compares integers and floats numerically and
rejects nonnumeric arguments.

Both `equal?` and `eq?` distinguish integers from floats:

```otium
(list (= 1 1.0) (equal? 1 1.0) (eq? 1 1.0))
; => (#t #f #f)
```

### Comparing contents with `equal?`

Use [`equal?`](/reference/otium.core/#equal?) for string contents and recursive
comparisons of pairs, lists, and arrays. Corresponding values must have the same
type, including numbers inside collections.

```otium
(equal? "hello" (string-append "hel" "lo")) ; => #t
(equal? '(1 (2 3)) '(1 (2 3)))             ; => #t
(equal? [1 [2]] [1 [2]])                  ; => #t
(equal? {:a 1} {:a 1})                    ; => #f
```

Tables, bytevectors, buffers, and functions compare by identity even under
`equal?`. Separately created tables do not compare equal just because they
contain the same entries.

### Comparing identity with `eq?`

Use [`eq?`](/reference/otium.core/#eq?) to check whether two references name the
same object. This is especially useful for mutable values: changing the object
through one reference also changes what an alias sees.

```otium
(let ((a [1 2])
      (alias a)
      (b [1 2]))
  (list (eq? a alias)
        (eq? a b)
        (equal? a b)))
; => (#t #f #t)
```

Symbols and keywords are interned, so matching names compare equal with `eq?`:

```otium
(eq? 'ready 'ready) ; => #t
(eq? :ready :ready) ; => #t
```

Use `equal?` for strings, even when comparing literals. String identity does not
tell you whether their text matches.

### Table keys

Table key comparison is type-sensitive: `1` and `1.0` are different keys.
Strings and pairs compare by contents, but mutable keys such as arrays compare
by identity. Two arrays that satisfy `equal?` can therefore still be distinct
table keys.

<!-- generated from values.md.spec sha256:7eedb914f0ef02ffb5c4f6c05e01164497dcb26dc39d9f4acbed1dcc75e403e5 -->
