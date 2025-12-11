# otium/os

This is a simple operating system. It's built as a single executable, but there are separate
user-space processes (which should not access kernel code or memory). 

Currently, it targets three systems:
- RISC-V under QEMU with assembly to switch and store stack information by hand
- WASM on Emscripten, using the fiber library to switch processes
- POSIX; portions of the operating system (not the whole thing) can run on OSX
  for standalone utilities (Tcl interpreter) or unit tests

## Coding style

In general, try to avoid using #ifdef based on platform architecture outside of the
platform-specific files (those containing riscv or wasm in the name).

### C++ Style

**Header naming:**
- Use `.hpp` extension for C++ headers (e.g., `ot/user/string.hpp`, `ot/user/vector.hpp`)
- Use `.h` extension for C headers or headers that need C compatibility

**Formatting:**
After editing C++ files, run clang-format to ensure consistent formatting:

```bash
clang-format -i path/to/file.cpp
clang-format -i path/to/file.hpp
```

The project uses the clang-format configuration specified in `.clang-format`.

## Build System

The project uses **Meson** as its primary build system with **Just** as a convenient command runner.

### Quick Start

```bash
# Build all platforms (POSIX, RISC-V, WASM)
just compile-all

# Build and run individual platforms
just build-run-riscv    # RISC-V in QEMU
just build-run-wasm     # WASM in Node.js
```

### Build Configuration

Configuration is handled by Meson and generates platform-specific `ot/config.h` files in each build directory.

**Default configuration:**
- **Kernel program**: `KERNEL_PROG_SHELL` (interactive shell)
- **Log levels**: `LSOFT` for all subsystems (general, memory, process, IPC)
- **RISC-V**: VirtIO graphics backend, memory filesystem
- **WASM**: WASM graphics backend, memory filesystem
- **POSIX**: No graphics, no filesystem (standalone tools only)

**Configuration options:**
```bash
# Build with a specific kernel program
meson setup build-riscv --cross-file=cross/riscv32.txt \
  -Dkernel_prog=test_graphics

# Configure graphics backend
meson setup build-riscv --cross-file=cross/riscv32.txt \
  -Dgraphics_backend=test

# Configure log levels
meson setup build-riscv --cross-file=cross/riscv32.txt \
  -Dlog_ipc=loud \
  -Dlog_mem=silent

# Reconfigure an existing build
meson setup build-riscv --reconfigure -Dkernel_prog=test_hello

# Multiple options at once
meson setup build-riscv --cross-file=cross/riscv32.txt \
  -Dkernel_prog=test_graphics \
  -Dgraphics_backend=virtio \
  -Dlog_general=loud
```

Available options (see `meson_options.txt` for full list):
- `kernel_prog`: shell, test_hello, test_mem, test_alternate, test_ipc, etc.
- `graphics_backend`: auto (platform default), none, test, virtio, wasm
- `filesystem_backend`: auto (platform default), none, memory, fat
- `log_general`, `log_mem`, `log_proc`, `log_ipc`: silent, soft, loud

### Build Directories

Each platform has its own isolated build directory with platform-specific configuration:

- `build-posix/` - Native tools (tcl-repl, edit)
- `build-riscv/` - RISC-V kernel (os.elf)
  - `build-riscv/ot/config.h` - Generated with `OT_GRAPHICS_BACKEND_VIRTIO`
  - `build-riscv/runtime-config.sh` - Runtime configuration for QEMU scripts
- `build-wasm/` - WebAssembly build (os.js, os.wasm)
  - `build-wasm/ot/config.h` - Generated with `OT_GRAPHICS_BACKEND_WASM`
  - `build-wasm/runtime-config.sh` - Runtime configuration for Node.js scripts

### Direct Meson Usage

While `just` provides convenient shortcuts, you can use Meson directly:

```bash
# Setup a build directory
meson setup build-riscv --cross-file=cross/riscv32.txt

# Compile with verbose output
meson compile -C build-riscv -v

# Clean and reconfigure
rm -rf build-riscv
meson setup build-riscv --cross-file=cross/riscv32.txt
```

Cross-compilation files are in `cross/`:
- `cross/riscv32.txt` - RISC-V 32-bit with LLVM
- `cross/wasm.txt` - WebAssembly with Emscripten

### Legacy Build System

The original shell script build system (`compile-riscv.sh`, `compile-wasm.sh`, etc.) and `config.sh` are still present and functional, but **Meson is now the recommended build system**.

## Testing

The project has several types of tests:

### Snapshot Tests

Snapshot tests verify kernel behavior by comparing `TEST:` output lines against saved snapshots.

```bash
./test-snapshot.py                   # Run all tests
./test-snapshot.py --platform=riscv  # Test RISC-V only
./test-snapshot.py --update          # Update snapshots
```

Tests automatically use Meson's `-Dkernel_prog` option to run different test programs.

### Unit Tests

```bash
just test-unit  # Run doctest-based unit tests
```

### Lint Tests

```bash
./test-lint.sh  # Run clang-tidy and semgrep checks
```

See `TESTING.md` for detailed testing documentation.

## Source

The source of the operating system is written in C++. Source files live in the `./ot` directory.
Because kernel and user space programs may be compiled and run separately, `ot/kernel` contains
kernel code and `ot/user` contains user code. `ot/shared` contains shared process-agnostic code
including some C stdlib like functionality that can be run in either system.

## Subsystems

### Processes and scheduler

The operating system runs multiple processes. Processes have a PID (unsigned int), a name and a
state of UNUSED, RUNNABLE, TERMINATED, or IPC_WAIT.

The scheduler currently is a purely cooperative system where processes yield to the operating
system, which then selects the next process by PID and runs it. IPC operations bypass normal
round-robin scheduling by immediately switching to the target process.

When a process encounters a fault on RISC-V, the process will be terminated by the OS.

### Inter-Process Communication (IPC)

The kernel provides a synchronous IPC mechanism for request-reply communication between processes.

#### Data Structures

- `IpcMessage` - Request from sender to receiver
  - `pid` - Sender PID (filled by kernel)
  - `method_and_flags` - Combined field: upper bits = method ID, lower 8 bits = flags
  - `args[3]` - Three method-specific arguments

- `IpcResponse` - Reply from receiver to sender
  - `error_code` - Error status (`NONE`, `IPC__PID_NOT_FOUND`, `IPC__METHOD_NOT_KNOWN`)
  - `values[3]` - Three return values

- IPC Flags (occupy lower 8 bits of method_and_flags)
  - `IPC_FLAG_NONE` (0x00) - No special flags
  - `IPC_FLAG_HAS_COMM_DATA` (0x01) - Comm page contains messagepack data to be copied

- Helper Macros
  - `IPC_PACK_METHOD_FLAGS(method, flags)` - Combine method and flags for internal use
  - `IPC_UNPACK_METHOD(method_and_flags)` - Extract method ID
  - `IPC_UNPACK_FLAGS(method_and_flags)` - Extract flags

#### Syscalls

- `ou_ipc_send(pid, flags, method, arg0, arg1, arg2)` - Send request and block until reply
  - `flags` specifies IPC behavior (use `IPC_FLAG_NONE` for simple calls)
  - `method` is the method ID (must not use lower 8 bits - those are reserved for flags)
  - `arg0`, `arg1`, `arg2` are method-specific arguments passed inline
  - If `IPC_FLAG_HAS_COMM_DATA` is set, sender's comm page is copied to receiver's comm page
  - Immediately switches to target process if it's waiting in `ou_ipc_recv`
  - Returns `IpcResponse` with result or error
  - **Note**: Method and flags are packed into a single field internally to save registers

- `ou_ipc_recv()` - Wait for incoming IPC request
  - Blocks caller in `IPC_WAIT` state until message arrives
  - Returns `IpcMessage` with sender info and arguments
  - Use `IPC_UNPACK_METHOD()` and `IPC_UNPACK_FLAGS()` macros to extract from `method_and_flags`
  - If `IPC_FLAG_HAS_COMM_DATA` was set, comm page contains messagepack data from sender

- `ou_ipc_reply(response)` - Send reply and resume caller
  - Immediately switches back to the blocked sender
  - Sender resumes with the provided response

#### Out-of-Band Data Transfer

For methods requiring more than 3 arguments or complex data:

1. **Sender** writes messagepack data to its comm page using `CommWriter` or `MPackWriter`
2. **Sender** calls `ou_ipc_send()` with `IPC_FLAG_HAS_COMM_DATA` set
3. **Kernel** copies sender's comm page (4KB) to receiver's comm page
4. **Receiver** reads messagepack data from its comm page using `MPackReader`

This allows transmitting arbitrary structured data while keeping simple calls fast with inline arguments.

#### Register Optimization

To maximize inline data capacity across different architectures, method and flags are packed into a single field:
- Lower 8 bits: flags (supports up to 256 flag combinations)
- Upper bits: method ID (supports millions of methods on 32-bit, trillions on 64-bit)

This optimization saves one register, allowing:
- **RISC-V**: 3 inline arguments (a2, a4, a5) instead of 2
- **ARM Cortex-M33**: 2 inline arguments (r2, r3) instead of 1
- **WASM**: No constraint, but maintains API consistency

The packing/unpacking is handled transparently at the syscall boundary with soft assertions to warn if method IDs accidentally use the lower 8 bits.

#### Scheduling Behavior

IPC operations use immediate scheduling (`process_switch_to`) rather than cooperative scheduling:

1. Sender calls `ou_ipc_send` targeting a receiver
2. If receiver is in `IPC_WAIT`, kernel immediately switches to receiver
3. Receiver processes request and calls `ou_ipc_reply`
4. Kernel immediately switches back to sender with response

This bypasses normal round-robin scheduling, allowing synchronous request-reply patterns.

#### Platform-Specific Implementation

**RISC-V** (`ot/core/platform/platform-riscv.cpp`, `ot/core/platform/user-riscv.cpp`):
- IPC implemented as syscalls via `ecall` instruction
- `process_switch_to()` performs direct stack context switch using `switch_context()`
- Immediately swaps from sender → receiver → sender with no scheduler involvement
- Updates `current_proc` before switching, relies on stack swap to maintain correct context

**WASM** (`ot/core/platform/platform-wasm.cpp`, `ot/core/platform/user-wasm.cpp`):
- IPC implemented as direct function calls (no syscalls needed)
- `process_switch_to()` must go through scheduler fiber (cannot directly swap between process fibers)
- Uses `wasm_switch_to_process(prev, target)` which:
  1. Sets `scheduler_next_process = target` to hint scheduler
  2. Calls `yield()` from prev's fiber to return to scheduler
  3. Scheduler immediately picks target fiber
  4. After target replies and yields back, scheduler picks prev again
- **Critical**: `current_proc` must remain pointing to prev during yield (not target), or wrong fiber is swapped
- Behavior is synchronous and deterministic, matching RISC-V despite going through scheduler

**Key Difference**: WASM requires scheduler as intermediary for fiber swaps, but maintains same synchronous semantics as RISC-V through immediate scheduling hints.

#### Error Codes

- `IPC__PID_NOT_FOUND` - Target process doesn't exist or isn't running
- `IPC__METHOD_NOT_KNOWN` - Receiver doesn't recognize the method ID

#### Debugging

Enable IPC tracing with `LOG_IPC` config to see IPC operations and context switches.

#### Code Generation

The IPC system includes a code generator that creates type-safe client/server wrappers from service definitions.

**Service Definition** (`services/*.yml`):

```yaml
name: Fibonacci
methods:
  - name: calc_fib
    params:
      - name: n
        type: int
    returns:
      - name: result
        type: int
  - name: calc_pair
    params:
      - name: n
        type: int
    returns:
      - name: fib_n
        type: int
      - name: fib_n_plus_1
        type: int
```

**Generated Files**:

- `ot/user/gen/fibonacci-client.hpp` - Client wrapper with type-safe method calls
- `ot/user/gen/fibonacci-server.hpp` - Server interface to implement
- `ot/user/gen/fibonacci-server-impl.hpp` - Server dispatch logic

**Usage Example**:

```cpp
// Client side
FibonacciClient client(fibonacci_pid);
auto result = client.calc_fib(10);
if (result.is_ok()) {
  oprintf("fib(10) = %d\n", result.value());
}

// Server side
struct FibonacciServerImpl : public FibonacciServer {
  Result<int, ErrorCode> calc_fib(int n) override {
    // Implementation
    return ok(result);
  }
};

void fibonacci_server_main() {
  FibonacciServerImpl impl;
  impl.run();  // Process requests in loop
}
```

**Type System**:

Service definitions support these parameter types:
- `int` - Signed integer (maps to `intptr_t`)
- `uint` - Unsigned integer (maps to `uintptr_t`)

Return values use `Result<T, ErrorCode>` for error handling.

**Method IDs**:

Method IDs auto-increment starting at `0x1000`, incrementing by `0x100` to avoid conflict with the 8-bit flags field. Reserved method IDs (below `0x1000`) include:
- `IPC_METHOD_SHUTDOWN` (`0x0100`) - Universal shutdown method

**Universal Shutdown**:

All generated servers inherit from `ServerBase` which provides automatic shutdown handling. Clients can call `client.shutdown()` to cleanly terminate a server.

**Build Integration**:

Add services to `build-common.sh`:
```bash
IPC_SERVICES="fibonacci calculator"
```

The build system automatically runs the code generator and includes generated files.

### Memory

The memory is a simple page system. Pages are kept in a free list. Processes never free pages, but
when processes are killed or exit the pages are returned to the OS. Pages can be readable, writable
and executable and processes can only access pages directly belonging to them, but this enforcement
only happens on RISC-V (as WASM doesn't have a mechanism for doing so).

### Filesystem

The OS provides a filesystem abstraction through an IPC-based server process. Available backends:

- **memory** - In-memory filesystem (volatile, for testing)
- **fat** - FAT32 filesystem via FatFs library (RISC-V only, uses VirtIO block device)

Configure with `-Dfilesystem_backend=<backend>`.

See `agent-docs/filesystem.md` for detailed documentation including:
- FatFs integration and configuration
- Disk I/O layer implementation
- IPC API reference
- Testing with disk images
- Creating FAT32 test images

### Graphics

The OS includes a framebuffer-based graphics system with multi-application support. The graphics server manages a 32-bit BGRA framebuffer and renders a taskbar showing all registered apps.

**Key features:**
- Multi-app support (up to 10 apps, most recent is active)
- Automatic dead process reaping
- 28px taskbar with TTF font rendering
- `app::Framework` utility class for drawing (primitives, bitmap and TTF fonts)

**Quick example:**
```cpp
GraphicsClient gfx(graphics_pid);
gfx.register_app("myapp");

while (running) {
  if (gfx.should_render().value()) {
    auto fb = gfx.get_framebuffer();
    // Draw to framebuffer...
    gfx.flush();
  }
  ou_yield();
}
```

**Backends:** test, virtio (RISC-V), wasm

See `agent-docs/graphics-apps.md` for detailed documentation including the application framework, memory requirements, and graphics server internals.

## Testing

### Unit tests

Unit tests use the doctest framework and run on the host system. They test core functionality like memory management, process management, data structures, and the TCL interpreter.

```bash
just test-unit  # Run all unit tests with Meson
```

The tests compile to a `unit-test` executable in the POSIX build directory and include comprehensive coverage of:
- Memory allocation and page recycling
- Process creation and lookup
- String and vector containers
- MessagePack serialization
- TCL interpreter
- Printf implementation
- Result type error handling

### Snapshot tests

The kernel uses snapshot testing to verify behavior across different test modes and platforms (RISC-V and WASM). Tests automatically use Meson's configuration system to select which kernel program to run.

```bash
./test-snapshot.py                   # Run all tests on all platforms
./test-snapshot.py --platform=riscv  # Test RISC-V only
./test-snapshot.py --update          # Update snapshots after changes
```

The test runner captures lines starting with `TEST:` from the kernel output and compares them against saved snapshots in the `snapshots/` directory. Each test runs in an isolated build directory (`build-riscv-test/`, `build-wasm-test/`) to avoid interfering with development builds.

See `TESTING.md` for complete testing documentation.

## Tcl

The operating system includes a minimal, self-contained Tcl interpreter implementation that runs in userspace. The interpreter is designed to work in a freestanding environment without standard library dependencies.

### Design

The Tcl implementation (`ot/user/tcl.h` and `ot/user/tcl.cpp`) provides:

- **Custom container classes**: `tcl::string`, `tcl::string_view`, and `tcl::vector<T>` that use only `malloc`/`free`/`realloc`
- **No standard library dependencies**: Uses placement new and custom allocators instead of `<string>`, `<vector>`, etc.
- **Lightweight parser**: Handles Tcl syntax including variables (`$var`), command substitution (`[cmd]`), and string quoting
- **Interpreter**: Maintains command registry, variable scopes (call frames), and evaluation state

### Memory Management

The Tcl interpreter uses the TLSF (Two-Level Segregated Fit) memory allocator:

1. The shell allocates contiguous pages from the OS (currently 10 pages = 40KB)
2. TLSF provides `malloc`/`free`/`realloc` implementations on top of this pool
3. All Tcl objects (strings, vectors, commands) allocate from this pool
4. Memory allocation failures are detected and cause graceful exit with error messages

### Core Commands

The interpreter includes these built-in commands:

- **I/O**: `puts` - print output
- **Variables**: `set` - get/set variables
- **Control flow**: `if`, `while`, `break`, `continue`, `return`
- **Procedures**: `proc` - define new commands
- **Arithmetic**: `+`, `-`, `*`, `/`
- **Comparison**: `==`, `!=`, `>`, `<`, `>=`, `<=`

### Shell Program

The shell (`ot/user/prog-shell.cpp`) provides an interactive REPL:

1. Initializes TLSF memory pool on startup
2. Creates Tcl interpreter and registers core commands
3. Reads input character-by-character (supports backspace)
4. Evaluates complete lines when Enter is pressed
5. Displays results or error messages
6. Custom commands can be added (e.g., `quit`, `crash` for testing)

### Usage Example

```tcl
> set x 10
result: 10
> set y 20
result: 20
> + $x $y
result: 30
> proc double {n} { return [* $n 2] }
result:
> double 5
result: 10
```

### Standalone REPL

A POSIX-compatible standalone Tcl REPL can be built for testing on the host system:

```bash
./build-posix.sh
./bin/tcl-repl              # Interactive mode
./bin/tcl-repl script.tcl   # Run script
```

This uses the same Tcl core but with POSIX I/O and the bestline library for line editing.

## Text Editor

The OS includes a minimal vim-like text editor that runs as a standalone POSIX tool. It uses termbox2 for terminal rendering and has three modes: NORMAL, INSERT, and COMMAND (entered with `;`).

```bash
# Build and run
meson compile -C build-posix edit
./build-posix/edit <filename>
```

See `agent-docs/edit.md` for detailed documentation.
