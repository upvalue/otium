# ✨ Strings, symbols and keywords

## Work with strings and characters

Write strings in double quotes and characters with `#\`. A character is a
separate type from a one-character string.

```otium
(char? #\a)       ; => #t
(char? "a")       ; => #f
(string #\a #\é #\🙂) ; => "aé🙂"
```

Strings support escapes such as `\n`, `\t`, `\"`, and `\\`. For character
literals, use names such as `#\space`, `#\newline`, and `#\tab`.

Strings are immutable. Their lengths and indexes count Unicode code points, not
bytes. Indexes start at zero; a visible character made from several code points
occupies several positions.

```otium
(string-length "héllo")    ; => 5
(get "aé🙂" 1)             ; => #\é
(substring "aé🙂z" 1 3)    ; => "é🙂"
```

`substring` excludes the end position; omit it to take the rest of the string.
`get` returns `nil` for an out-of-range index. Use `string-ref` when an invalid
index should raise an error.

Convert to a list of characters when you need to process characters
individually:

```otium
(string->list "aé🙂")       ; => (#\a #\é #\🙂)
(list->string '(#\x #\y))   ; => "xy"
(char->integer #\🙂)        ; => 128578
(integer->char 955)         ; => #\λ
```

Compare characters with `char=?`, `char<?`, and their ordering counterparts. Use
`char-alphabetic?`, `char-numeric?`, and `char-whitespace?` to classify them.
Character classification and case conversion are ASCII-only on the C runtime.

```otium
(char-upcase #\a)           ; => #\A
(char-whitespace? #\tab)    ; => #t
(digit-value #\7)           ; => 7
(digit-value #\a)           ; => nil
```

## Clean up and split text

String operations return new strings. Use `equal?` to compare their contents.

```otium
(string-trim " \té\n")             ; => "é"
(string-upcase "abC")              ; => "ABC"
(string-replace "abab" "b" "X")    ; => "aXaX"
(string-contains? "a🙂b" "🙂")      ; => #t
(string-starts-with? "abc" "ab")    ; => #t
(string-ends-with? "abc" "bc")      ; => #t
```

`string-downcase` converts in the other direction. As with character conversion,
string case conversion is ASCII-only on the C runtime. Searches and replacements
use literal text, not regular expressions; `string-replace` replaces every
occurrence.

`string-split` returns an array. Without a separator, it splits on whitespace
runs. An explicit separator preserves empty fields.

```otium
(string-split "  a\tb\nc ")   ; => ["a" "b" "c"]
(string-split "a,,b," ",")    ; => ["a" "" "b" ""]
(string-join ":" [1 2 3])     ; => "1:2:3"
```

The separator comes first in `string-join`. Each element is converted to its
display form.

For numeric input, use `string->number`. It accepts surrounding whitespace and
returns `nil` when conversion fails.

```otium
(string->number " 42\n")   ; => 42
(string->number "nope")    ; => nil
```

## Build strings

Use `string-append` to concatenate strings. Use `str` when the pieces include
other kinds of values. Neither inserts spaces.

```otium
(string-append "hello" " " "world") ; => "hello world"
(str "a" 1 :k)                     ; => "a1:k"
(str #\a "bc")                     ; => "abc"
```

`str` leaves top-level strings unquoted. Strings inside collections keep their
quotes and escapes.

For text assembled a piece at a time, use a mutable buffer:

```otium
(let ((out (buffer "é")))
  (buffer-push! out "🙂" 7)
  (buffer->string out))
; => "é🙂7"
```

`buffer-push!` appends display forms, just like `str`. `buffer->string` returns
an immutable string; you can keep using the buffer afterward.

## Format and output text

For ordinary messages, build text with `str` or `string-join`. To send values to
output:

-  `display` and `print` separate arguments with spaces, without adding a
  newline.
- `println` also adds a newline.
- `write` uses printed representations, including quotes around strings.
- `newline` emits a newline.

See [Files and I/O](io.md) for working with ports and files.

### Format Otium source with `otium.format`

`otium.format` formats Otium source text. It is not a message-template or
`printf` library.

Load it and pass source text plus layout rules to `format-text`:

```otium
(require otium.format :as format)

(format/format-text "(foo  a\n   b)" format/builtin-rules)
; => "(foo a b)\n"

(format/format-text "(def (f x) x)" format/builtin-rules)
; => "(def (f x)\n  x)\n"
```

The result is a string, so you can save it or display it yourself. The formatter
keeps comments, normalizes spacing and line breaks, and adds a final newline to
nonempty source.

## Choose symbols or keywords

Use strings for text, symbols for program names or symbolic data, and keywords
for tags and table keys.

An unquoted symbol refers to a binding. Quote it when you want the name itself:

```otium
(let ((status "ready"))
  (list status 'status :status))
; => ("ready" status :status)
```

Keywords start with `:` and evaluate to themselves. They need no quote, which
makes them convenient for fixed field names and choices such as `:ready` or
`:pending`.

A keyword can also look up its key in a table:

```otium
(:a {:a 7})   ; => 7
```

Use `eq?` to compare ordinary symbols or keywords by name. A symbol and a
keyword with the same spelling are different values. Use `symbol?` and
`keyword?` when you need to distinguish them.

See [Namespaces](namespaces.md) for symbols used to refer to names in other
modules.

## Convert text and names

Use `string->symbol` and `symbol->string` for explicit string/symbol
conversions. The broader `symbol` and `keyword` functions accept strings,
symbols, or keywords.

```otium
(string->symbol "status")   ; => status
(symbol->string 'status)    ; => "status"
(symbol :status)            ; => status
(keyword "status")          ; => :status
(keyword 'status)           ; => :status
```

Pass the name without a leading colon to `keyword`.

Use `name` to get plain text from any of these three types. For a keyword, it
omits the colon. Use `str` when you want its displayed spelling instead.

```otium
(name 'status)   ; => "status"
(name :status)   ; => "status"
(name "status")  ; => "status"
(str :status)    ; => ":status"
```

Creating a symbol produces a name value; it does not look up or evaluate the
binding with that name.

<!-- generated from strings.md.spec sha256:492e16aa051c76a51e9032c7b929c6de8405a204ea70726114c7dd7ae5cb77d0 -->
