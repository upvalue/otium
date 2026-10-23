# ✨ otos guide

otos is a small operating system for Otium. It is a unikernel: one program, the Otium C runtime, linked with a thin kernel layer into a single RISC-V image. It boots in QEMU straight into an Otium REPL on the serial console.

This guide covers building, booting and changing otos. For the language itself, see the [language guide](../language-guide.md). For the rest of the docs, see the [docs index](../README.md).

## What works today

- Booting on QEMU's `virt` machine as a 32-bit RISC-V (`rv32gc`) kernel, started by OpenSBI.
- An Otium REPL on the UART console, with line editing, history and multi-line input.
- `^C` interrupts running code. `^D` on an empty line shuts the machine down.
- Errors are rendered by `otium.diagnostics`, as in the host REPL.
- Requiring namespaces from the `lib/` tree, which is built into the image as read-only files.
- Monotonic time from the RISC-V `time` CSR, and wall-clock time from QEMU's Goldfish RTC.

There is no disk, network, display, writable file system or FFI. See [Limits and what is missing](#limits-and-what-is-missing).

## Layout

| Path | What it is |
| --- | --- |
| `otos/Justfile` | `build` and `run` recipes |
| `otos/meson.build` | Builds picolibc, compiler-rt builtins, the runtime and the kernel |
| `otos/cross/riscv32.txt` | Meson cross file for clang targeting `riscv32-unknown-elf` |
| `otos/src/boot.S` | Entry point and trap vector |
| `otos/src/kernel.ld` | Linker script and memory layout |
| `otos/src/kernel.c` | `kmain`, the REPL, error reporting, traps and SBI shutdown |
| `otos/src/ot-kernel.c` | Otium platform layer: UART console, clocks, memory and embedded files |
| `otos/src/line.c`, `otos/src/line.h` | Line editor |
| `otos/src/kernel.h` | Console and shutdown functions shared by the kernel files |
| `otos/src/files.h` | Layout of the embedded file table |
| `otos/tools/embed-files.py` | Generates the embedded file table from `lib/` |
| `otos/vendor/compiler-rt/` | Vendored compiler-rt builtins |
| `otos/subprojects/picolibc.wrap` | Fetches picolibc |

Two pieces of third-party code are used:

- **picolibc** 1.8.12 is the C library. Meson clones it from GitHub through `otos/subprojects/picolibc.wrap` into `otos/subprojects/picolibc/`. That directory is git-ignored. It is built single-threaded, without its own startup code (`picocrt=false`), without semihosting, and with `double` printf formatting.
- **compiler-rt builtins** live in `otos/vendor/compiler-rt/`. They are the generic sources from LLVM `llvmorg-22.1.8`, without the Apple and trampoline files. They provide the 64-bit integer and soft-float helpers that rv32 code calls. They are vendored because Homebrew LLVM does not ship a riscv32 `libclang_rt.builtins.a`.

## Prerequisites

- **LLVM from Homebrew.** `otos/cross/riscv32.txt` uses hard-coded paths under `/opt/homebrew/opt/llvm/bin` for `clang`, `llvm-ar`, `llvm-strip` and `llvm-objcopy`, and links with `-fuse-ld=lld`. The build uses C23 and `#embed`, so clang must support both. On another machine, change the paths in the cross file.
- **Meson** 1.6 or later, and Ninja.
- **Python 3**, to run `otos/tools/embed-files.py`.
- **git**, so Meson can fetch picolibc on the first setup.
- **QEMU** with `qemu-system-riscv32`. QEMU's bundled OpenSBI firmware is used (`-bios default`).
- **just**, to run the recipes. You can also run the commands by hand.

## Build and boot

Run the recipes from the `otos/` directory:

```sh
cd otos
just build   # meson setup (first time only) and meson compile
just run     # build, then boot in QEMU
```

`just build` runs `meson setup build --cross-file=cross/riscv32.txt` if `otos/build/` does not exist yet, then `meson compile -C build`. The kernel is `otos/build/otos.elf`.

`just run` boots it with:

```sh
qemu-system-riscv32 -machine virt -m 256M -bios default -nographic -no-reboot -kernel build/otos.elf
```

After OpenSBI's banner you should see something like:

```
otos: hart 0, device tree at 0x...
otos: runtime ready in N ms
otium repl (otos) - ^C interrupts, ^D shuts down
ot>
```

To leave, press `^D` on an empty line or call `(exit)`. Either one shuts the machine down through SBI. If the kernel hangs, QEMU's own escape `^A x` quits.

The default build type is `debugoptimized`. To change Meson options, reconfigure the existing build directory, or delete `otos/build/` and run `just build` again.

## How it boots

### Memory layout

`otos/src/kernel.ld` places the kernel at `0x80200000`. On QEMU `virt`, RAM starts at `0x80000000`, and OpenSBI takes the first 2 MB. The script assumes 256 MB of RAM. That must match the `-m 256M` flag in `otos/Justfile`, so change both together.

In order, the image contains:

- `.text`, with `.text.boot` kept first so `_start` is at the load address
- `.rodata`
- `.data`, which also defines `__global_pointer$`
- `.bss`, between `__bss_start` and `__bss_end`
- a 4 MB stack ending at `__stack_top`
- the heap, from `__heap_start` to `__heap_end` at the end of RAM

`.eh_frame` is discarded. picolibc's `malloc` takes memory from the heap region. The Otium runtime gets its memory through `malloc` too.

### Entry

OpenSBI jumps to `_start` in `otos/src/boot.S` in supervisor mode, with the hart id in `a0` and the device tree address in `a1`. `_start` then:

1. Loads `gp` with linker relaxation turned off, and sets `sp` to `__stack_top`.
2. Turns the FPU on by setting `sstatus.FS`, and clears `fcsr`. This must happen before any compiled code runs, because the ABI is `ilp32d` and the compiler may use float registers anywhere.
3. Points `stvec` at `trap_entry`.
4. Zeroes `.bss`.
5. Calls `kmain(hart, device_tree)`. If `kmain` ever returned, the hart would wait in a `wfi` loop.

### Kernel entry

`kmain` in `otos/src/kernel.c`:

1. Prints the hart id and device tree address. The device tree is not parsed.
2. Creates the runtime with `ot_create`, and prints how long that took.
3. Sets the namespace loader, the console poll hook for `^C`, and the `%reported-condition` native used for error reporting.
4. Runs the REPL.
5. Shuts down when the REPL returns.

### Console

The console is the 16550 UART at `0x10000000`. It is polled, with no interrupts:

- `console_write` waits for the transmit register to be empty before each byte, and turns `\n` into `\r\n`.
- `console_read_byte` spins until a byte arrives.
- picolibc's `stdin`, `stdout` and `stderr` are all one stream backed by these two functions, so `printf` and `puts` go to the UART.

### Traps

Every trap is fatal. `trap_entry` resets `sp` to the top of the stack, then calls `kernel_trap` with `scause`, `sepc` and `stval`. `kernel_trap` prints:

```
otos: trap cause=... pc=... value=...
```

and then shuts down with a failure reason. Interrupts are never enabled, so in practice a trap means a fault, such as a bad memory access or an illegal instruction.

### Shutdown

`sbi_shutdown` makes an `ecall` to the SBI System Reset extension (`0x53525354`) to shut the machine down. It passes "system failure" as the reason when asked to report one, and no reason otherwise. picolibc's `exit` and `abort` end in `_exit`, which calls `sbi_shutdown` with failure set when the status is not 0. The REPL reports failure when it ends because of `(exit n)` with a non-zero `n`. Because QEMU runs with `-no-reboot`, a reset ends the QEMU process.

## How the runtime is embedded

### What is built from `c/`

`otos/meson.build` builds a static library, `otium`, from these files in `c/src/`:

- `otium.c`, the runtime. It embeds `lib/otium/core.ot` (the prelude) with `#embed`, so the prelude is in the image even without the file table.
- `ot-gc-common.c` and `ot-gc-semi.c`, the semispace collector (`-DOT_GC_SEMI`).
- `ot-ffi.c`, built without `OT_WITH_FFI`. Requiring `otium.ffi` raises an "unavailable" error.
- `ot-cli.c`. It is built because the runtime registers the CLI modules at startup.

`c/src/ot-posix.c` is replaced by `otos/src/ot-kernel.c`. `main.c`, the generational collector and the extension demo are not built.

The runtime is compiled with these settings:

| Define | Value | Meaning |
| --- | --- | --- |
| `OT_HEAP_INIT` | 1 MB | Initial heap |
| `OT_HEAP_MAX` | 16 MB | Heap limit |
| `OT_MAX_DEPTH` | 10000 | Call depth limit |
| `OT_GC_TIMING` | 0 | No GC timing |

### The platform layer

`otos/src/ot-kernel.c` provides the functions that `c/src/otium.h` expects from a host:

- `ot_host_alloc`, `ot_host_realloc` and `ot_host_free`, built on picolibc's `calloc`, `realloc` and `free`.
- `ot_platform_monotonic_ns`, which reads the `time` CSR. It assumes QEMU `virt`'s 10 MHz timebase instead of reading it from the device tree.
- `ot_platform_current_second`, which reads the Goldfish RTC at `0x101000`.
- `ot_default_write`, which writes to the console.
- `ot_platform_read_file` and `ot_platform_io_host`, which serve the embedded files.

### Embedded files

At build time, `otos/tools/embed-files.py` walks the repository's `lib/` directory and writes `files.c` into the build directory. It skips dot files. Each file becomes a byte array filled with `#embed` and ended with a NUL byte. The table, laid out as in `otos/src/files.h`, maps each absolute path to its data and length. For example, `lib/otium/json.ot` becomes `/lib/otium/json.ot`.

The script also writes a depfile that lists the directories as well as the files. So editing, adding or removing a file under `lib/` regenerates the table on the next `just build`.

The I/O host in `otos/src/ot-kernel.c` presents the table as a read-only file system:

- Paths are resolved against `/`, which is the only working directory. `.`, `..` and repeated slashes are handled.
- A directory is any path prefix of an embedded file. Directories can be listed and inspected.
- Up to 16 files can be open at once. Their handles start at 3.
- Handles 0, 1 and 2 are the console. Only handle 0 can be read, only handles 1 and 2 can be written, and all three count as terminals.
- Opening a file for output, removing, renaming and making directories all fail with a permission error.

### Loading namespaces

`load_namespace` in `otos/src/kernel.c` is the runtime's loader. It turns dots in the namespace name into slashes, puts `/lib/` in front and `.ot` at the end. So `otium.json` loads from `/lib/otium/json.ot`. There is no other load path. Only namespaces under `lib/` can be required.

The loader also records the file path as the source name, so errors point at the right file. See the [language guide](../language-guide.md) for how `require` and namespaces behave.

## The REPL

The REPL lives in `repl` in `otos/src/kernel.c`.

- The prompt is `ot> `. While a form is still incomplete, it changes to `..> `, and each new line is added to the pending input.
- Input is evaluated with `ot_eval_partial`. When a form is complete, its value is printed with `ot_repr_to`.
- When evaluation fails, the condition is passed to `otium.diagnostics/render`, which is loaded from the embedded `lib/` on first use. If rendering fails, otos prints `error: ` followed by the condition's message.
- `^C` at the prompt throws away pending input.
- `^C` while code is running interrupts it, and the REPL prints `interrupted`. The runtime calls the console poll hook every few thousand VM steps. The hook reads waiting UART bytes and looks for `^C`. Other bytes it reads are saved as typeahead for the next read, up to 256 of them.
- `(exit)` or `(quit)` ends the REPL and shuts down, as does `^D` on an empty line.

A short session:

```lisp
(def (square x) (* x x))
(square 12)
(require otium.extra :as extra)
((extra/partial + 1) 2)
```

Each form's value is printed under it. `(square 12)` prints `144`.

## Line editing

`otos/src/line.c` is a small line editor. It handles control keys and the usual escape sequences for arrow, Home, End and Delete keys:

| Key | Action |
| --- | --- |
| Left, `^B` / Right, `^F` | Move one character |
| Home, `^A` / End, `^E` | Move to start / end of line |
| Backspace | Delete before the cursor |
| Delete, `^D` | Delete at the cursor (`^D` on an empty line is end of input) |
| `^K` | Delete to end of line |
| `^U` | Delete to start of line |
| `^W` | Delete the previous word (spaces, tabs and parentheses end a word) |
| Up, `^P` / Down, `^N` | Walk history |
| `^L` | Clear the screen |
| `^C` | Cancel the line |

Details:

- History holds 100 lines. Empty lines, and lines that repeat the previous entry, are not added. The line being typed is kept while you walk history, and Down brings it back.
- Each line holds at most 1023 bytes. Extra input is dropped.
- Tab inserts a space. Other control characters are ignored.
- UTF-8 is kept together when moving and deleting. The editor assumes each character is one column wide and the line fits on one terminal row. After each edit it redraws the whole line, except when typing at the end of the line.

## Making changes

- **Otium code under `lib/`:** edit it and run `just run`. The file table is regenerated, and `require` sees the new file. Because the files are baked in, `:reload` in a running kernel reloads the same code. To pick up an edit, rebuild and boot again.
- **Kernel C code:** add new source files to the `executable('otos.elf', ...)` list in `otos/meson.build`.
- **Runtime limits:** change `runtime_args` in `otos/meson.build`. These flags are used for both the runtime library and the kernel sources.
- **Memory size:** change `LENGTH` in `otos/src/kernel.ld` and `-m` in `otos/Justfile` together.
- **Toolchain paths:** edit `otos/cross/riscv32.txt`, then delete `otos/build/` so Meson sets up again.

otos builds the C runtime, which is experimental and can lag behind the Chez Scheme runtime. Code that works on Chez may need features the C runtime does not have yet.

## Limits and what is missing

- **One machine.** Only QEMU `virt` on rv32 is supported. The UART, RTC and timebase addresses are hard-coded, and the device tree is ignored.
- **One hart, no interrupts.** Interrupts are never enabled, and there is no timer, no scheduler and no threads. All I/O is polled. Any trap stops the machine.
- **No memory protection.** The kernel runs in supervisor mode with paging off, and the runtime shares one address space with everything else.
- **Small, fixed memory.** The Otium heap stops at 16 MB, the call depth limit is 10000, and the semispace collector is the only collector built.
- **Read-only files.** Only `lib/` is available, and only as it was at build time. Nothing can be written, and there is no disk driver.
- **No devices beyond the console.** There is no network, display, input or block device, and no virtio support.
- **No FFI.** `otium.ffi` is unavailable, so namespaces that depend on it (for example SDL, SQLite and Objective-C) cannot work.
- **No development server.** The live tools used with `--dev` on the host are not available. Changes reach the kernel only by rebuilding.
- **Bounded integers.** Like the C runtime on any host, otos has no bignums. See the [language guide](../language-guide.md).
- **Simple console editing.** Lines longer than one terminal row, and wide characters, are not drawn correctly.
