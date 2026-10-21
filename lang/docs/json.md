# JSON

Load `(require otium.json :as json)`. `(json/parse text)` reads one JSON
value; `(json/encode value)` returns compact JSON text.

Objects become insertion-ordered tables with string keys, arrays become Otium
arrays, and booleans become `#t` and `#f`. JSON null becomes `json/null`, a
unique symbol that survives module reloads. This preserves null-valued object
fields: storing Otium `nil` in a table would delete them. The encoder accepts
both `json/null` and `nil` as null.

```scheme
(require otium.json :as json)
(def config (json/parse "{\"enabled\":true,\"limit\":null}"))
(eq? (get config "limit") json/null) ; => #t
(json/encode config) ; => "{\"enabled\":true,\"limit\":null}"
```

The parser accepts JSON whitespace, escapes and Unicode surrogate pairs.
It rejects trailing data, malformed numbers, unescaped control characters and
unpaired surrogates. Duplicate object keys use the last value. Errors include
the character offset. Nesting is limited to 256 levels; the encoder uses the
same limit, which also bounds traversal of cyclic values.

Numbers use the runtime's integer and floating-point types. Chez supports
arbitrary-size integers; C has bounded integers. Floating-point conversion can
round or underflow. Non-finite numbers are rejected. Encoding requires string
object keys and rejects unsupported values such as lists, keywords and functions.

The implementation uses the shared character and buffer APIs. On Chez, scanning
is linear in the text length, and unescaped strings need only one substring
copy. C string indexing scans UTF-8 from the beginning, so large inputs are
slower there. Input and output are whole strings; use `read-file` to load a file.
Streaming ports can be added when Otium's port API is implemented.
