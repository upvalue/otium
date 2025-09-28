# kernel

the beginnings of an OS kernel in Rust

State: Right now it's just a hello world / echo program. Next up will be porting the Tcl interpreter
from [this article](https://upvalue.io/posts/trialing-zig-and-rust-by-writing-a-tcl-interpreter/) to
run standalone.

## Running

> ./run-riscv32.sh

and

> ./run-wasm.sh

Will build and run the program on the given platform. You'll need QEMU for RISCV32 and NodeJS for
running on WASM; it's only been run and tested on OSX with the appropriate compiler toolchains
installed.

