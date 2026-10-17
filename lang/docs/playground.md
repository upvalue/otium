# Try Otium

Edit an example and run it. This is the C runtime compiled to WebAssembly, running locally in your browser.

> ✨ LLM-generated draft. This playground follows the C dialect.

The session keeps definitions between runs. Each run displays printed output followed by the value of its last expression. Use `(describe 'map)` to look up a binding from inside the language.

There is no SDL, native FFI, or host filesystem access. Loading your own namespace files is not wired up. A run is stopped after five seconds; this resets the session. Output is capped at 64 KiB per run. Browser integers use a 31-bit signed representation (−1,073,741,824 through 1,073,741,823), narrower than on a 64-bit native build.
