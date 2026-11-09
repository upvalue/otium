# ✨ Strings, symbols and keywords

Use strings for text you want to read, transform or show to someone. Symbols
represent names in code; keywords make convenient tags and table keys. A
character is a single Unicode code point, such as `#\a` or `#\λ`.

## Strings and characters

Strings use double quotes and are immutable. Operations such as trimming and
replacement return new strings. Common escapes include `\n` for a newline, `\t`
for a tab, `\"` for a quote and `\\` for a backslash.

String positions are zero-based and count Unicode code points, not bytes. A
displayed character made from several code points still occupies several
positions.

```otium
(def text "aé🙂")

(string-length text)       ; => 3
(string-ref text 1)        ; => #\é
(substring text 1 3)       ; => "é🙂"
```

[`substring`](/reference/otium.core/#substring) includes the start position and
excludes the end. Omit the end to take the rest of the string. `string-ref`
raises an error for an out-of-range index; `get` returns `nil` when the position
is absent.

Characters are distinct from one-character strings. Use `string` to assemble
characters, or convert between strings and character lists to process characters
individually.

```otium
(string->list text)                    ; => (#\a #\é #\🙂)
(list->string '(#\a #\é #\🙂))           ; => "aé🙂"
(string #\H #\i #\newline)              ; => "Hi\n"

(char-upcase #\a)                      ; => #\A
(char-whitespace? #\tab)                ; => #t
(char->integer #\λ)                    ; => 955
(integer->char 955)                    ; => #\λ
```

Named character literals include `#\space`, `#\newline` and `#\tab`. Character
comparisons such as `char=?` and `char<?` compare code points. Do not assume
case conversion and character classification handle every Unicode language.

## Taking text apart

[`string-split`](/reference/otium.core/#string-split) returns an array. Supply a
literal separator, or omit it to split on runs of whitespace.
[`string-join`](/reference/otium.core/#string-join) takes the separator first.

```otium
(def tags (string-split (string-trim " red,green,blue ") ","))

tags                              ; => ["red" "green" "blue"]
(string-join " / " tags)           ; => "red / green / blue"
(string-split " red\tgreen  blue ")
; => ["red" "green" "blue"]
```

Search and replacement functions work with literal text, not regular
expressions. `string-replace` replaces every occurrence.

```otium
(string-replace "red-green-red" "red" "blue")
; => "blue-green-blue"

(string-starts-with? "notes.ot" "notes") ; => #t
(string-ends-with? "notes.ot" ".ot")     ; => #t
(string-contains? "notes.ot" "tes")      ; => #t
```

Use `equal?` to compare string contents. Case conversion is useful for simple
text normalization.

```otium
(string-upcase "hello")                    ; => "HELLO"
(string-downcase "HELLO")                  ; => "hello"
(equal? "hello" (string-downcase "HELLO"))  ; => #t

(string->number "42")     ; => 42
(string->number "hello")  ; => nil
```

`string->number` returns `nil` when the text is not a number. `number->string`
converts a number back to its display text.

## Building strings

Use `string-append` when all the pieces are strings.
[`str`](/reference/otium.core/#str) accepts other values too and joins their
display forms without inserting spaces. `string-join` inserts a separator
between items.

```otium
(string-append "Hello, " "Ada" "!") ; => "Hello, Ada!"
(str "Items: " 3)                   ; => "Items: 3"
(string-join ", " '(1 2 3))         ; => "1, 2, 3"
```

To assemble text a piece at a time, use a mutable
[`buffer`](/reference/otium.core/#buffer). Append with `buffer-push!`, then get
a string with `buffer->string`.

```otium
(let ((out (buffer "Items:")))
  (for ((item :in '(10 20 30)))
    (buffer-push! out " " item))
  (buffer->string out))
; => "Items: 10 20 30"
```

`buffer->string` gives you a snapshot. You can keep appending to the buffer
without changing that string.

## Output and formatting

`println` displays its arguments separated by spaces and adds a newline.
`display` does the same without the newline. `write` uses printed
representations, so strings retain quotes and escapes.

```otium
(println "Hello," "Ada")
; prints: Hello, Ada

(write "Hello, Ada")
(newline)
; prints: "Hello, Ada"
```

Display leaves top-level strings unquoted. Strings inside collections retain
their quotes and escapes, including when you convert a collection with `str`.

### Formatting Otium source with `otium.format`

`otium.format` formats Otium source text. For ordinary messages containing
values, use the string-building functions above.

Load the module and pass source text and layout rules to
`format-text`. It returns a string
without printing or evaluating it.

```otium
(require otium.format :as fmt)

(fmt/format-text
  "(def (greet name) (str \"Hello, \" name))"
  fmt/builtin-rules)
; => "(def (greet name)\n  (str \"Hello, \" name))\n"
```

The built-in rules lay out definitions and other forms while preserving
comments. Nonempty formatted source ends with a newline. You can display the
returned string or save it using the functions described in [Files and
I/O](io.md).

## Choosing strings, symbols or keywords

A string holds text whose contents you process: a message, filename or
user-entered label.

Use a symbol for a name in code, including code represented as data. An unquoted
symbol refers to a binding; quote it to obtain the symbol itself.

Keywords are useful for fixed tags or field names. They evaluate to themselves,
so they need no quote. They can also look themselves up in a table.

```otium
(def status :ready)

status        ; => :ready
'status       ; => status

(:status {:status :ready :message "Ready to start"})
; => :ready

(eq? 'ready :ready) ; => #f
```

The symbol `ready`, keyword `:ready` and string `"ready"` are different values.
Ordinary symbols and keywords are interned: repeated names of the same kind
identify the same value, so `eq?` is suitable for comparing them. Compare string
contents with `equal?`.

See [Bindings](bindings.md) for name lookup and [Data
structures](data-structures.md) for tables.

## Converting names to and from text

Use `string->symbol` and `symbol->string` for explicit string/symbol
conversions. `symbol` and `keyword` accept strings, symbols or keywords and
produce the requested kind of name.

```otium
(string->symbol "ready") ; => ready
(symbol->string 'ready) ; => "ready"

(keyword "ready")       ; => :ready
(keyword 'ready)        ; => :ready
(symbol :ready)         ; => ready
```

When constructing a keyword from a string, pass the bare name without a leading
colon. Constructing a symbol does not look up its binding.

[`name`](/reference/otium.core/#name) extracts name text from a symbol or
keyword and leaves a string unchanged. For keywords, it omits the colon; `str`
preserves the colon in the display form.

```otium
(name :ready)  ; => "ready"
(name 'ready)  ; => "ready"
(name "ready") ; => "ready"
(str :ready)   ; => ":ready"
```

<!-- generated from strings.md.spec sha256:54f0afa9bf72dcabfdaa1590c1175b59d1e3c794e5553d39517a9881fea56c20 -->
