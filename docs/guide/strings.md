# ✨ Strings, symbols and keywords

Programs handle two different kinds of text: user-facing text that you slice,
search and format, and short names that identify bindings, table keys or
tags in your code.

Otium separates those jobs across four types:

- A string (`"hello"`) is an immutable sequence of Unicode characters for
  arbitrary text.
- A character (`#\a`) is a single Unicode code point, returned when you index
  into a string or iterate through its characters.
- A symbol (`'status`) is an interned name for program identifiers and quoted
  code.
- A keyword (`:status`) is an interned, self-evaluating name for table keys,
  options and tags.

Unlike Scheme strings, Otium strings cannot be mutated in place; when you
assemble text in stages, you append to a mutable buffer and freeze it into a
string at the end.

## Strings and characters

Strings are enclosed in double quotes and support `\n`, `\t`, `\r`, `\0`, `\e`,
`\"`, `\\` and `\xHEX;` escapes (see [Syntax](syntax.md)). Every string length
and position in Otium counts Unicode code points rather than UTF-8 bytes.

[`string-length`](/reference/otium.core/#string-length) returns the number of
characters in a string. [`get`](/reference/otium.core/#get) and
[`string-ref`](/reference/otium.core/#string-ref) return the character at a
zero-based index; `get` returns `nil` when the index is out of range, while
`string-ref` raises an error. The sequence helpers
[`first`](/reference/otium.core/#first), [`last`](/reference/otium.core/#last)
and [`rest`](/reference/otium.core/#rest) also accept strings:

```otium
(string-length "aé🙂")                  ; => 3
(get "aé🙂" 1)                          ; => #\é
(get "aé🙂" 9)                          ; => nil
(string-ref "aé🙂" 2)                   ; => #\🙂
(list (first "abc") (last "abc") (rest "abc"))
; => (#\a #\c "bc")
```

Character literals begin with `#\`, such as `#\a`, `#\λ`, `#\space` or
`#\newline`. [`char->integer`](/reference/otium.core/#char->integer) and
[`integer->char`](/reference/otium.core/#integer->char) convert between a
character and its Unicode code point. You can compare characters by code point
with [`char=?`](/reference/otium.core/#char=?) and
[`char<?`](/reference/otium.core/#char<?), test ASCII character classes with
[`char-alphabetic?`](/reference/otium.core/#char-alphabetic?),
[`char-numeric?`](/reference/otium.core/#char-numeric?) and
[`char-whitespace?`](/reference/otium.core/#char-whitespace?), convert an ASCII
digit with [`digit-value`](/reference/otium.core/#digit-value), or change case
with [`char-upcase`](/reference/otium.core/#char-upcase) and
[`char-downcase`](/reference/otium.core/#char-downcase):

```otium
(list (char->integer #\λ)
      (integer->char 955)
      (char<? #\a #\b #\c)
      (char-alphabetic? #\z)
      (char-whitespace? #\tab)
      (digit-value #\7)
      (digit-value #\x)
      (char-upcase #\a))
; => (955 #\λ #t #t #t 7 nil #\A)
```

Use [`string`](/reference/otium.core/#string) to build a string directly from
character arguments, and [`string->list`](/reference/otium.core/#string->list)
or [`list->string`](/reference/otium.core/#list->string) to convert between a
string and a list of characters when you want to process characters with list
functions:

```otium
(list (string #\o #\k)
      (string->list "ok")
      (list->string '(#\o #\k)))
; => ("ok" (#\o #\k) "ok")
```

## Taking strings apart and searching

[`substring`](/reference/otium.core/#substring) extracts a slice from a start
index up to an optional end index. Out-of-range indices are clamped to the
string's bounds. [`string-split`](/reference/otium.core/#string-split) splits a
string into an array of substrings on a literal separator, or on runs of
whitespace when called with one argument.
[`string-trim`](/reference/otium.core/#string-trim) strips leading and trailing
whitespace:

```otium
(list (substring "otium/core.ot" 6 10)
      (substring "otium/core.ot" 6))
; => ("core" "core.ot")

(list (string-split "a,b,c" ",")
      (string-split "  one   two  three ")
      (string-trim "  hello\n"))
; => (["a" "b" "c"] ["one" "two" "three"] "hello")
```

To inspect a string's contents, use
[`string-starts-with?`](/reference/otium.core/#string-starts-with?),
[`string-ends-with?`](/reference/otium.core/#string-ends-with?) and
[`string-contains?`](/reference/otium.core/#string-contains?). All three match
literal substrings, not regular expressions.
[`string-index`](/reference/otium.core/#string-index) returns the character
offset of the first match at or after an optional start index, or `nil` when
the substring is absent:

```otium
(list (string-starts-with? "core.ot" "core")
      (string-ends-with? "core.ot" ".ot")
      (string-contains? "core.ot" "re.")
      (string-index "banana" "na")
      (string-index "banana" "na" 3))
; => (#t #t #t 2 4)
```

[`string-replace`](/reference/otium.core/#string-replace) replaces every
occurrence of a literal substring,
[`string-upcase`](/reference/otium.core/#string-upcase) and
[`string-downcase`](/reference/otium.core/#string-downcase) return case-folded
copies, and [`string<?`](/reference/otium.core/#string<?) compares strings
lexicographically:

```otium
(list (string-replace "a-b-c" "-" "_")
      (string-upcase "Otium")
      (string-downcase "Otium")
      (string<? "alpha" "beta"))
; => ("a_b_c" "OTIUM" "otium" #t)
```

Use [`equal?`](/reference/otium.core/#equal?) or
[`string=?`](/reference/otium.core/#string=?) to test whether two strings have
the same characters (see [Values and types](values.md)).

[`string->number`](/reference/otium.core/#string->number) parses an integer or
floating-point number, ignoring surrounding whitespace, and returns `nil` if
the text is not a valid number.
[`number->string`](/reference/otium.core/#number->string) formats a number as a
string:

```otium
(list (string->number "42")
      (string->number " 3.5 ")
      (string->number "42px")
      (number->string 42))
; => (42 3.5 nil "42")
```

## Building strings

Otium has several ways to put strings together, depending on what values you
have and how many pieces you are combining:

- [`string-append`](/reference/otium.core/#string-append) concatenates strings
  and raises an error if any argument is not a string.
- [`str`](/reference/otium.core/#str) converts any values to their display form
  and concatenates them without spaces. Top-level strings and characters appear
  unquoted, while strings inside collections keep their quotes and escapes.
- [`string-join`](/reference/otium.core/#string-join) joins the display forms
  of a list or array with a separator string.
- [`string-repeat`](/reference/otium.core/#string-repeat) repeats a string a
  given number of times.

```otium
(string-append "otium" "." "core")      ; => "otium.core"
(str "port " 8080 " is " :open)         ; => "port 8080 is :open"
(str ["a" 'b :c])                       ; => "[\"a\" b :c]"
(string-join ", " ["red" "green" "blue"])
; => "red, green, blue"
(string-repeat "=-" 3)                  ; => "=-=-=-"
```

When you assemble a string in a loop, repeated `string-append` or `str` calls
allocate a new string on every step. A buffer is a mutable string builder that
collects pieces in place. Create one with
[`buffer`](/reference/otium.core/#buffer)
(optionally seeded with an initial value), append values in display form with
[`buffer-push!`](/reference/otium.core/#buffer-push!), and call
[`buffer->string`](/reference/otium.core/#buffer->string) to produce the final
string:

```otium
(let ((out (buffer "items:")))
  (for-each (fn (x) (buffer-push! out " " x)) '(1 2 3))
  (buffer->string out))
; => "items: 1 2 3"
```

Calling `buffer->string` leaves the buffer usable if you want to keep pushing
to it. When a function writes to a port rather than a buffer, you can also
capture its output as a string with
[`open-output-string`](/reference/otium.core/#open-output-string) or
[`with-output-to-string`](/reference/otium.core/#with-output-to-string); see
[Files and I/O](io.md).

## Formatting output with `otium.format`

When a tool generates or rewrites Otium code as text, the
[otium.format](/reference/otium.format/) namespace formats that source text
using the same layout rules as `./otium fmt`. It keeps forms on one line when
they fit within [`width`](/reference/otium.format/#width) (120 columns) and
breaks multi-form bodies, definitions and wide calls across indented lines
while preserving comments.

Pass a string of source forms and a rule table such as
[`builtin-rules`](/reference/otium.format/#builtin-rules) to
[`format-text`](/reference/otium.format/#format-text):

```otium
(require otium.format :as format)

(format/format-text "(def (add x y) (+ x y))" format/builtin-rules)
; => "(def (add x y)\n  (+ x y))\n"
```

If your project defines macros whose bodies should indent like a built-in form
such as `when` or `let`,
[`project-rules`](/reference/otium.format/#project-rules)
reads `(format (like known custom ...))` forms from `project.ot` source text
and returns an extended rule table:

```otium
(let ((rules (format/project-rules "(format (like when with-lock))")))
  (format/format-text "(with-lock mtx (step-one) (step-two))" rules))
; => "(with-lock mtx\n  (step-one)\n  (step-two))\n"
```

## Choosing between symbols, keywords and strings

Although symbols, keywords and strings are all written with characters, they
play different roles in an Otium program:

- Use a symbol when naming a binding or working with code as data, such as in
  macros (see [Macros](macros.md)), [`eval`](/reference/otium.core/#eval) or
  [`describe`](/reference/otium.core/#describe). Evaluating an unquoted symbol
  looks up its binding; quote it as `'status` to obtain the symbol itself.
- Use a keyword when you want a lightweight label, option name, state tag or
  table key. Keywords start with `:`, evaluate to themselves without quoting,
  and can be called as functions to look themselves up in a table.
- Use a string for external or arbitrary text: file contents, paths, user input,
  messages and JSON keys.

```otium
(def status :ready)
(list status 'status)
; => (:ready status)

(def packet {:kind :ping :id 7})
(list (get packet :kind)
      (packet :id)
      (:kind packet))
; => (:ping 7 :ping)
```

Symbols and keywords are interned: every occurrence of `'ready` is the same
symbol object, and every occurrence of `:ready` is the same keyword object. You
can compare them in constant time with [`eq?`](/reference/otium.core/#eq?), and
match them directly in [`case`](/reference/otium.core/#case) or
[`match`](/reference/otium.core/#match) patterns. A symbol and a keyword are
never equal to each other, and strings should always be compared by contents
with `equal?`:

```otium
(list (eq? 'ready 'ready)
      (eq? :ready :ready)
      (eq? 'ready :ready)
      (equal? "ready" "ready"))
; => (#t #t #f #t)
```

## Converting between strings, symbols and keywords

When data crosses a boundary, such as parsing command-line flags or generating
symbols in a macro, you often need to convert between text and names.

[`name`](/reference/otium.core/#name) returns the underlying string of a
string, symbol or keyword (without a keyword's leading colon):

```otium
(list (name "host")
      (name 'host)
      (name :host))
; => ("host" "host" "host")
```

[`symbol`](/reference/otium.core/#symbol) and
[`keyword`](/reference/otium.core/#keyword) coerce a string, symbol or keyword
to the target type:

```otium
(list (symbol "host")
      (symbol :host)
      (keyword "host")
      (keyword 'host))
; => (host host :host :host)
```

When you want functions that require a specific input type, use
[`symbol->string`](/reference/otium.core/#symbol->string),
[`string->symbol`](/reference/otium.core/#string->symbol) and
[`keyword->string`](/reference/otium.core/#keyword->string):

```otium
(list (symbol->string 'otium.core/map)
      (string->symbol "otium.core/map")
      (keyword->string :port))
; => ("otium.core/map" otium.core/map "port")
```

<!-- generated from strings.md.spec sha256:54f0afa9bf72dcabfdaa1590c1175b59d1e3c794e5553d39517a9881fea56c20 -->
