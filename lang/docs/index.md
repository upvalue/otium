# A small Lisp, made to be changed.

Otium is an experimental language for small runtimes and live programs. It combines a Scheme-like core with mutable collections, namespaces, and functions you can redefine while a program is running.

> ✨ LLM-generated draft. This page is a starting point for maintained language documentation.

[Try Otium in your browser](playground.md) · [Read the user guide](guide.md)

## Start with an expression

```scheme
(def (square x) (* x x))
(map square [1 2 3 4 5])
```

The result is `[1 4 9 16 25]`. Parentheses call functions. Square brackets build arrays. Functions are values you can pass to other functions.

## Keep the program running

Namespace bindings are live. Redefine a function and existing callers see the new definition:

```scheme
(def (greeting) "hello")
(def (welcome) (greeting))
(def (greeting) "hello again")
(welcome)
```

The result is `"hello again"`. This is part of the language's binding model.

## Work with data

Lists, arrays, and insertion-ordered tables are built in. Keywords make useful table keys. `nil` represents absence, and a missing lookup returns `nil` unless you supply a default.

```scheme
(def person {:name "Ada" :language "Otium"})
(get person :name)
```

Use the [language reference](reference.md) to look up collection operations, control forms, macros, and conditions.

## Two runtimes

The C runtime is designed for embedding. The Chez runtime builds on Chez Scheme. They share the reader, expander, derived library, and runnable examples. [The portability guide](portability.md) describes their common contract and remaining differences. The browser runs the C runtime compiled to WebAssembly.

The project is experimental. The [specification archive](../specs/language.md) records language design, and may lag behind implementation. The guide and runnable examples are maintained separately.
