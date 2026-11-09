# ✨ Syntax

Otium keeps Lisp's parenthesized calls and quoted data, but its collection
delimiters have distinct meanings: square brackets construct arrays, and braces
construct tables. `nil` is a separate value, not another name for the empty
list.

The reader turns source text into values called *forms*. A program evaluates
those forms in order. Knowing what the reader produces helps you distinguish
calls from literal data.

## Whitespace, comments and delimiters

Source is UTF-8. Whitespace separates tokens; indentation and line breaks do not
group expressions. A semicolon starts a comment that continues to the end of the
line. There are no block comments or Scheme-style `#;` datum comments.

```otium
(+ 1 ; a comment ends at the newline
   2)
; => 3
```

Parentheses, square brackets and braces must close with their matching
delimiter. They are not interchangeable.

The characters `(`, `)`, `[`, `]`, `{`, `}`, `;`, `"`, `'`, backquote and comma
end ordinary atom tokens. Commas are quotation syntax, not optional separators
between collection elements.

## Atoms

Atoms include numbers, strings, characters, symbols, keywords, booleans and
`nil`. Except for symbols, these values evaluate to themselves.

### Numbers

Numbers include signed decimal integers, hexadecimal integers beginning with
`0x`, and floating-point numbers with decimal points or exponents.

```otium
[42 -7 0xff 3.5 -.5 1e3]
; => [42 -7 255 3.5 -0.5 1000.0]
```

Use `0xff`, not Scheme's `#xff`. Rational and complex literals such as `1/2` and
`1+2i` are not supported. A token that begins like a number must be a valid
number: `12abc` is an error, not a symbol.

### Strings and characters

Double quotes enclose strings, which may contain Unicode text and span lines.
The supported escapes are:

- `\n`, `\t`, `\r`: newline, tab and carriage return.
- `\0`, `\e`: NUL and escape.
- `\"`, `\\`: a double quote and a backslash.
- `\xHEX;`: a Unicode code point in hexadecimal, with a required terminating
  semicolon.

Unknown escapes are errors.

```otium
"say \"hi\"\\\n\x3bb;"
; => "say \"hi\"\\\nλ"
```

Characters begin with `#\`. The following character can be a delimiter. Named
characters are `#\space`, `#\newline`, `#\tab`, `#\return` and `#\nul`. Names
are case-sensitive. A hexadecimal character uses `#\x` followed by digits,
without a semicolon.

```otium
[#\a #\( #\space #\x41]
; => [#\a #\( #\space #\A]
```

### Symbols and keywords

Symbols name bindings when evaluated. Names commonly contain punctuation:
`foo-bar!`, `empty?`, `+` and `->` are symbols. A name such as `term/write`
refers to a binding through a namespace or alias; see
[Namespaces](namespaces.md).

Quote a symbol to use it as data rather than look up its binding.

Keywords begin with `:`, as in `:name`. They evaluate to themselves and are
commonly used as table keys or tags. A bare `:` is invalid.

### Nil and booleans

`nil` means absence. `#t` and `#f` are the boolean values; the longer spellings
`#true` and `#false` also work.

```otium
(list nil #t #f '())
; => (nil #t #f ())
```

`nil`, `#f` and the empty list `()` are three distinct values. Only `nil` and
`#f` are falsy. The empty list, zero and empty strings are truthy.

## Lists, arrays and tables

Parentheses read as lists. In evaluated code, a nonempty list normally
represents a call: its first element identifies the operation, and its remaining
elements are arguments. Ordinary function calls evaluate their arguments;
special forms and macros control how their arguments are evaluated.

Square brackets construct mutable arrays. Braces construct mutable tables from
alternating keys and values. Array elements and table keys and values are
evaluated.

```otium
(let ((n 3))
  (list (+ n 1)
        [n (+ n 1)]
        {:count n :next (+ n 1)}))
; => (4 [3 4] {:count 3 :next 4})
```

Use `(list ...)` to build a list from evaluated elements, and quotation for
literal list data.

`#u8(...)` constructs a bytevector and evaluates its elements:

```otium
#u8(1 (+ 1 2))
; => #u8(1 3)
```

See [Data structures](data-structures.md) for working with these collections.

## Quotation

A leading apostrophe is shorthand for `quote`. It returns the following form as
data without evaluating it.

```otium
'size
; => size

'(+ 1 2)
; => (+ 1 2)
```

Within a list, a standalone dot marks its final tail rather than another
element. This is Lisp's dotted-pair notation:

```otium
'(a b . c)
; => (a b . c)
```

The reader expands brackets into an `array` call and braces into a `table` call.
Quoting them therefore produces constructor lists, not constructed collections.

```otium
'[1 2]
; => (array 1 2)

'{:count 3}
; => (table :count 3)
```

Backquote introduces a *quasiquote*: a data template where comma evaluates and
inserts one value, and comma-at evaluates and splices a list's elements.

```otium
(let ((n 3)
      (tail '(4 5)))
  `(items ,n ,@tail))
; => (items 3 4 5)
```

Backquote reads as `quasiquote`, comma as `unquote`, and comma-at as
`unquote-splicing`.

The prefix ``#` `` reads as `syntax-quote` and is used for macro templates. See
[Macros](macros.md) for its name-resolution rules.

## Reading printed values

The result comments above use the representation shown by the REPL and
[`write`](/reference/otium.core/#write): strings have quotes and escapes, and
characters retain their `#\` notation. Floats retain a decimal point or
exponent.

[`println`](/reference/otium.core/#println) displays top-level strings and
characters as their raw text instead. Inside collections, they still use their
quoted representations. Printed opaque values such as `#<fn name>` describe
runtime objects; they are not source literals you can paste back into a program.

<!-- generated from syntax.md.spec sha256:d6a1f7be88c95d29a6c0e577c5b874a67a5fcd0b21e4656e88c0d1b84722df3d -->
