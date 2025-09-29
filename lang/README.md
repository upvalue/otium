# lang

A simple Lisp-derived language with C-like syntax.

Status: It can run some basic examples such as the Fibonacci number calculator and Raylib
graphical demos.

The current implementation is in TypeScript running under Bun (for prototyping -- this
will eventually gain a standalone runtime and compiler). It's very much been thrown together
and is still changing a lot. It works by outputting JS which can then be run anywhere you
can run JS (at present, the output is relatively implementation agnostic though this might
change).

> bun install

Before running anything.

> bun lexer.ts scratch.ot

Lexes a whole file and prints out tokens.

> bun parser.ts scratch.ot

Parses a whole file and prints out an internal representation of expressions.

> bun translate.ts scratch.ot

Translates the file to JavaScript.

> bun eval.ts scratch.ot

Evaluates the file 

In principle, each of these are independent steps that build on eachother (e.g. lexer has no dependencies, parser depends on the lexer and so on)

# Examples

There are some examples under `./examples`

- fib.ot - the classic fibonacci calculator meme code
- raylib-ball.ot - Renders a ball with Raylib and moves it around

To run the raylib examples is a bit more involved:

> bun translate.ts example/raylib-ball.ot > raylibdemo/raylib-ball.js
> cd raylibdemo && npm install && node raylib-ball.js

As `node-raylib` didn't seem to work under Bun.

# Testing

> bun test 

to run test suites. Most tests are snapshot tests.
