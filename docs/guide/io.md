# ✨ Files and I/O

Ports give programs one interface for reading and writing text in files, at the
terminal, and in strings held in memory. A function that accepts an output port
can write to a file or standard output; you can test it by writing to a string
instead. Input ports let you work through a large file a piece at a time.

A port is a value representing an open source or destination. An input port
supplies data; an output port receives it. There are file ports, standard input
and output ports, and in-memory string and bytevector ports. Text operations use
UTF-8.

Most reading and writing operations accept an optional port. Without one, they
use `current-input-port` or `current-output-port`, which default to standard
input and output. These are [dynamic params](params.md), so a caller can
redirect a function's output without changing the function.

## Read a whole file

[`read-file`](/reference/otium.core/#read-file) returns a file's entire contents
as a string and closes the file for you. Use it when the whole file fits
comfortably in memory.

For these examples, create a text file called `io-guide-notes.txt` containing
two lines, `tea` and `coffee`, each ending with a newline. Example paths are
relative to your working directory.

```otium
(read-file "io-guide-notes.txt")
; => "tea\ncoffee\n"
```

## Read through a port

Use [`with`](/reference/otium.core/#with) to keep a file port open while you
work. It returns its body's result and closes the port when the body exits,
including when an error interrupts it. You don't need an import.

[`read-line`](/reference/otium.core/#read-line) returns a line without its line
ending. At the end of input, it returns the EOF object. Test it with
`eof-object?`: EOF is truthy, and an empty string is a valid blank line.

This counts lines without keeping the whole file in memory:

```otium
(with ((in (open-input-file "io-guide-notes.txt")))
  (let loop ((line (read-line in)) (count 0))
    (if (eof-object? line)
        count
        (loop (read-line in) (+ count 1)))))
; => 2
```

## Write a file

`write-string` writes text exactly as supplied. `newline` writes a line ending.
Both take an optional output port as their final argument.

Define a writer that accepts its destination:

```otium
(def (write-note out text)
  (write-string text out)
  (newline out))

(with ((out (open-output-file "io-guide-copy.txt")))
  (write-note out "tea"))

(read-file "io-guide-copy.txt")
; => "tea\n"
```

[`open-output-file`](/reference/otium.core/#open-output-file) creates a file if
needed and replaces its existing contents by default. Closing the port flushes
buffered output.

To add text to the existing contents, open the file for appending:

```otium
(with ((out (open-output-file "io-guide-copy.txt"
                             {:if-exists :append})))
  (write-note out "coffee"))

(read-file "io-guide-copy.txt")
; => "tea\ncoffee\n"
```

## Work with strings

An output string port collects text in memory. Use `get-output-string` before
closing it to retrieve that text. The same `write-note` function works
unchanged:

```otium
(with ((out (open-output-string)))
  (write-note out "tea")
  (get-output-string out))
; => "tea\n"
```

An input string port lets you use the same reading operations on text you
already have:

```otium
(with ((in (open-input-string "tea\ncoffee\n")))
  (read-line in))
; => "tea"
```

### Capture ordinary output

[`with-output-to-string`](/reference/otium.core/#with-output-to-string) calls a
zero-argument function with a fresh string port as its current output port. It
returns the captured text:

```otium
(with-output-to-string
  (fn () (println "tea" 2)))
; => "tea 2\n"
```

For an existing destination, rebind `current-output-port` directly. The previous
current output port is restored when `with-params` exits.

```otium
(with ((out (open-output-string)))
  (with-params ((current-output-port out))
    (println "tea" 2))
  (get-output-string out))
; => "tea 2\n"
```

## Print values

To print values rather than raw text, use these functions:

- `display` prints human-readable forms, without quotes around strings.
- `write` prints Otium representations, including string quotes and escapes.
- `println` prints like `display`, then adds a newline.

All three accept multiple values, separated by spaces, and write to the current
output port.

```otium
(with-output-to-string
  (fn ()
    (display "tea" 2)
    (newline)
    (write "tea")
    (newline)
    (println "coffee" 3)))
; => "tea 2\n\"tea\"\ncoffee 3\n"
```

Unlike Scheme, `display` does not take a port. Neither does `write`: passing a
port prints it as another value. Use the port-first
[`display-to`](/reference/otium.core/#display-to) and
[`write-to`](/reference/otium.core/#write-to), or rebind the current output
port. For exact text without added spaces, use `write-string`.

## When opening fails

If a file is missing, inaccessible, or otherwise cannot be opened, Otium raises
a `file-error` condition rather than returning `nil`. The condition includes the
path, operation, and a reason such as `:not-found` or `:permission`.

A surrounding `with` still closes any ports it has already opened. See [Errors
and recovery](errors.md) for catching conditions and inspecting their messages.

## Binary data

Bytevectors hold raw bytes, and bytevector ports let you read and build binary
data in memory. File ports also support byte operations; there is no separate
text-only port kind. See [`bytevector`](/reference/otium.core/#bytevector),
[`open-input-bytevector`](/reference/otium.core/#open-input-bytevector),
[`read-bytevector`](/reference/otium.core/#read-bytevector), and
[`write-bytevector`](/reference/otium.core/#write-bytevector).

## JSON

[otium.json](/reference/otium.json/) converts between JSON text and Otium
values. `json/parse` takes a string; `json/encode` returns one. Objects become
tables with string keys, arrays become arrays, and JSON `null` is `json/null`,
not `nil`.

Write a JSON file by encoding a value and sending the text to a port:

```otium
(require otium.json :as json)

(with ((out (open-output-file "io-guide-settings.json")))
  (write-string
    (json/encode {"drink" "tea" "extra" json/null})
    out))
```

Read it back with `read-file` and `json/parse`:

```otium
(def settings
  (json/parse (read-file "io-guide-settings.json")))

(get settings "drink")
; => "tea"

(eq? (get settings "extra") json/null)
; => #t
```

<!-- generated from io.md.spec sha256:d46f3750684481cfbf0d73ce02b06ccd4c4cb03afcb70f200ee71ca12ba79d7c -->
