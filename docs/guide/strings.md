# ✨ Strings, symbols and keywords

Use strings for text you read, transform or show to people, symbols for names in
code and quoted data, and keywords for tags and table keys. These are distinct
values: `"status"`, `'status` and `:status` are not interchangeable.

## Strings and characters

Strings are immutable sequences of Unicode code points, written in double
quotes. Common escapes include `\n` for newline, `\t` for tab, `\"` for a quote
and `\\` for a backslash.

A character is a separate value: `#\a` is a character, while `"a"` is a string.
Spaces, newlines and tabs have named forms: `#\space`, `#\newline` and `#\tab`.

String indexes start at zero and count code points, not bytes. A code point is
not always a whole visible character; a letter and a combining accent can occupy
two positions.

```otium
(def text "aé🙂z")

(string-length text)  ; => 4
(string-ref text 2)   ; => #\🙂
(substring text 1 3)  ; => "é🙂"
```

[`substring`](/reference/otium.core/#substring) includes the start position and
excludes the end. Omit the end to take the rest of the string.
[`string-ref`](/reference/otium.core/#string-ref) raises an error outside the
string; `get` returns `nil` instead.

You can assemble characters with `string`, or convert between a string and a
list of characters:

```otium
(string #\h #\i)               ; => "hi"
(string->list "hi")            ; => (#\h #\i)
(list->string '(#\h #\i))      ; => "hi"

(char-whitespace? #\tab)       ; => #t
(char-upcase #\a)              ; => #\A
```

## Taking text apart

[`string-split`](/reference/otium.core/#string-split) returns an array. Give it
a literal separator, or omit the separator to split on runs of whitespace.
[`string-join`](/reference/otium.core/#string-join) takes the separator first.

```otium
(def fields
  (string-split (string-trim " Ada,Lin,Sam \n") ","))

fields                       ; => ["Ada" "Lin" "Sam"]
(string-join ", " fields)     ; => "Ada, Lin, Sam"

(string-split "one  two\tthree")
; => ["one" "two" "three"]
```

String transformations return new strings. `string-trim` removes surrounding
whitespace; `string-upcase` and `string-downcase` change case. Replacement and
search functions use literal text, not regular expressions.

```otium
(string-downcase "HELLO")                ; => "hello"
(string-replace "a-b-a" "a" "x")         ; => "x-b-x"

(string-contains? "notes.ot" ".ot")      ; => #t
(string-starts-with? "notes.ot" "notes") ; => #t
(string-ends-with? "notes.ot" ".ot")     ; => #t
(equal? "Ada" "Ada")                     ; => #t
```

Use `equal?` to compare string contents. For numeric text,
[`string->number`](/reference/otium.core/#string-%3Enumber) returns `nil` when
parsing fails:

```otium
(string->number "42")       ; => 42
(string->number "unknown")  ; => nil
(number->string 42)         ; => "42"
```

## Building strings

[`string-append`](/reference/otium.core/#string-append) accepts only strings.
[`str`](/reference/otium.core/#str) accepts other values too, converting them to
their display forms. Neither inserts spaces.

```otium
(string-append "Hello, " "Ada" "!")  ; => "Hello, Ada!"
(str "Found " 3 " names")           ; => "Found 3 names"
```

Use `string-join` when you already have a sequence of pieces. To accumulate text
in a loop, use a buffer, a mutable string builder.
[`buffer-push!`](/reference/otium.core/#buffer-push!) appends display forms;
`buffer->string` returns the assembled string. The buffer remains usable after
conversion.

```otium
(let ((out (buffer "Names: ")))
  (for-each
    (fn (person) (buffer-push! out person "; "))
    '("Ada" "Lin"))
  (buffer->string out))
; => "Names: Ada; Lin; "
```

## Output and formatting

For a message, build its text with `str` or print values directly.
[`println`](/reference/otium.core/#println) separates its arguments with spaces
and adds a newline:

```otium
(println "Found" 3 "names")
; prints: Found 3 names
```

`display` behaves the same way without the final newline.
[`write`](/reference/otium.core/#write) shows a value's readable representation,
including quotes and escapes:

```otium
(write "Ada\nLin")
; prints: "Ada\nLin"
```

Display leaves top-level strings unquoted. Strings inside collections retain
quotes and escapes, including when a collection is converted with `str`.

### Formatting Otium source

[otium.format](/reference/otium.format/) formats Otium source text. It is not a
message-template or `printf` library. Use it when you have code as a string and
want consistent spacing and indentation.

Pass the source and the built-in layout rules to
[`format-text`](/reference/otium.format/#format-text):

```otium
(require otium.format :as fmt)

(fmt/format-text
  "(def (greet name) (str \"Hello, \" name))"
  fmt/builtin-rules)
; => "(def (greet name)\n  (str \"Hello, \" name))\n"
```

The result is a string; formatting does not evaluate the code. The formatter
preserves comments and adds a final newline to nonempty source.

## Symbols and keywords

A symbol normally names a binding. Quote it when you want the symbol itself as
data. Keywords begin with `:` and evaluate to themselves, so they need no quote.

```otium
(def status "Ready")

status   ; => "Ready"
'status  ; => status
:status  ; => :status
```

Symbols are for representing code, naming bindings or working with symbolic
data. Keywords suit application-defined tags and keys, such as `:ready` and
`:status`. For user-entered names, messages, paths and other text, use strings.

Keywords can also look themselves up in a table:

```otium
(:status {:status :ready})  ; => :ready
```

See [Data structures](data-structures.md) for more on tables.

## Converting text and names

[`symbol`](/reference/otium.core/#symbol) and
[`keyword`](/reference/otium.core/#keyword) accept strings, symbols or keywords
and create the corresponding name value. Converting text to a symbol does not
look up a binding.

```otium
(symbol "status")  ; => status
(keyword "status") ; => :status
(keyword 'status)  ; => :status
(symbol :status)   ; => status
```

[`name`](/reference/otium.core/#name) extracts the name as text, without a
keyword's colon. A string passes through unchanged. Use `str` instead when you
want the displayed spelling:

```otium
(name 'status)   ; => "status"
(name :status)   ; => "status"
(name "status")  ; => "status"
(str :status)    ; => ":status"
```

The explicit conversions are also available:

```otium
(string->symbol "status") ; => status
(symbol->string 'status)  ; => "status"
(keyword->string :status) ; => "status"
```

Symbols with the same name share identity, as do keywords with the same name.
Symbol and keyword identities remain separate:

```otium
(eq? (symbol "status") 'status) ; => #t
(equal? 'status :status)        ; => #f
```

<!-- generated from strings.md.spec sha256:54f0afa9bf72dcabfdaa1590c1175b59d1e3c794e5553d39517a9881fea56c20 -->
