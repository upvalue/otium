# Otium

Otium is an experimental little vibe-coded Lisp (but this README was written by
a human).

It is most directly descended from Scheme, but takes some ideas from Clojure
and Janet in terms of practicality. 

Its goal is being a fairly dynamic language with a runtime suitable for
multi-MB embedded systems (eg ESP32, RP2350) or as an extension language for C
programs. It also includes a Chez Scheme implementation which is preferred for 
normal computing use as it's much higher performance.

There are some examples in ./examples, including handwritten code, to show how
it feels to use.

The C runtime has a moving, generational garbage collector. Because getting
this code right is difficult, it has a custom libclang-based linter that flags
whether GC code is written correctly.

## Credits

Otium's `gen` collector is an adaptation of
[toit](https://github.com/toitlang/toit)'s garbage collector, originally from
Dartino.
