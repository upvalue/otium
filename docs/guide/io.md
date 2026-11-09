# ✨ Files and I/O

A program may read text from a file or the terminal and write it to a file,
standard output, or a string it is building. Ports give these sources and
destinations the same interface. A function that accepts an output port can
write to any of them, so you can test its output using a string. An input port
lets you process a large file a piece at a time.

A port is a value representing an open source or destination. Each port is
either an input port or an output port. File ports connect to files, and
standard ports connect to standard input, output, and error. String ports read
or accumulate text in memory. Text operations use UTF-8.

Many operations accept a port as their last argument. If you omit it, they use
`current-input-port` or `current-output-port`. These are [dynamic
params](params.md), so a caller can redirect a function's input or output
without changing the function.

## Reading a whole file

Use [read-file](/reference/otium.core/#read-file) to read the entire contents as
a string:

```otium
(read-file "notes.txt")
```

It opens and closes the file for you. This is convenient for small files. For
larger ones, open a port and read only what you need.

## Reading line by line

[with](/reference/otium.core/#with) binds a resource for the duration of its
body and closes it when the body exits, including when an error occurs. It
returns the body's value.

This counts lines without keeping the whole file in memory:

```otium
(with ((in (open-input-file "notes.txt")))
  (let loop ((count 0))
    (let ((line (read-line in)))
      (if (eof-object? line)
          count
          (loop (+ count 1))))))
```

[read-line](/reference/otium.core/#read-line) returns a string without its line
ending. A blank line is `""`. When no input remains, it returns the EOF object,
which you test with `eof-object?`. EOF is truthy, so testing the result as a
boolean will not stop the loop.

## Writing a file

[open-output-file](/reference/otium.core/#open-output-file) creates a file or
replaces an existing file's contents. `write-string` writes text exactly as
given; `newline` adds a line ending. Closing the port flushes buffered output.

This helper writes to any output port:

```otium
(def (write-note out text)
  (write-string text out)
  (newline out))

(with ((out (open-output-file "notes.txt")))
  (write-note out "Buy coffee"))
```

To keep the existing contents and add text at the end, open the file for
appending:

```otium
(with ((out (open-output-file "notes.txt" {:if-exists :append})))
  (write-note out "Call Sam"))
```

## Reading and building strings

String ports use the same operations as file ports.
[open-input-string](/reference/otium.core/#open-input-string) lets a reader
consume text already in memory:

```otium
(with ((in (open-input-string "Buy coffee\nCall Sam\n")))
  (read-line in))
; => "Buy coffee"
```

An output string port accumulates text, letting you check `write-note` without
creating a file. Use `get-output-string` inside the scope, before the port
closes:

```otium
(with ((out (open-output-string)))
  (write-note out "Buy coffee")
  (get-output-string out))
; => "Buy coffee\n"
```

For code that uses the current output port,
[with-output-to-string](/reference/otium.core/#with-output-to-string) calls a
zero-argument function and returns its output:

```otium
(with-output-to-string
  (fn () (println "Buy coffee")))
; => "Buy coffee\n"
```

You can also rebind the current output port yourself:

```otium
(with ((out (open-output-string)))
  (with-params ((current-output-port out))
    (println "Buy coffee"))
  (get-output-string out))
; => "Buy coffee\n"
```

`with-params` restores the previous port when its body exits. The same rebinding
works with a file port.

## Displaying text and writing values

[display](/reference/otium.core/#display) writes human-readable values, with
strings appearing without quotes. [write](/reference/otium.core/#write) uses
Otium notation, including string quotes and escapes.
[println](/reference/otium.core/#println) uses display formatting and adds a
newline. These functions accept multiple values, separate them with spaces, and
write to the current output port.

```otium
(with-output-to-string
  (fn ()
    (println "items:" 3)
    (write "coffee")))
; => "items: 3\n\"coffee\""
```

Unlike Scheme, `display` does not take a port. `(display value port)` prints
both values. For an explicit destination, use the port-first
[display-to](/reference/otium.core/#display-to) and
[write-to](/reference/otium.core/#write-to):

```otium
(with ((out (open-output-string)))
  (display-to out "item:" "coffee")
  (newline out)
  (write-to out "coffee")
  (get-output-string out))
; => "item: coffee\n\"coffee\""
```

Use `write-string` for exact text without added spaces or value formatting.

## When opening fails

If a file is missing or access is denied, opening it raises a `file-error`; it
does not return `nil`. The condition includes a message, the path, and a reason
such as `:not-found` or `:permission`.

Let the error propagate, or handle it with `try` when your program has a useful
response. See [Errors and recovery](errors.md). Ports already acquired by `with`
are still closed if a later operation fails.

## Binary data

Bytevectors hold binary data, and byte ports let you read or build it in memory.
File ports support byte operations too; there is no separate text-only port
kind. See [bytevector](/reference/otium.core/#bytevector),
[open-input-bytevector](/reference/otium.core/#open-input-bytevector),
[open-output-bytevector](/reference/otium.core/#open-output-bytevector), and the
[read-bytevector](/reference/otium.core/#read-bytevector) and
[write-bytevector](/reference/otium.core/#write-bytevector) operations.

## JSON

json/parse converts a string to Otium values.
JSON objects become tables with string keys, and arrays become Otium arrays.
JSON `null` is json/null, not `nil`.

Read a JSON file by passing its contents to the parser:

```otium
(require otium.json :as json)

(json/parse (read-file "settings.json"))
```

json/encode returns JSON text. Write that
string through a file port:

```otium
(with ((out (open-output-file "settings.json")))
  (write-string
    (json/encode {"theme" "dark" "last-opened" json/null})
    out)
  (newline out))
```

<!-- generated from io.md.spec sha256:d46f3750684481cfbf0d73ce02b06ccd4c4cb03afcb70f200ee71ca12ba79d7c -->
