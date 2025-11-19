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

### Graphics

The OS includes a framebuffer-based graphics system accessible via IPC. Graphics are provided by a server process that manages the framebuffer and exposes it to clients.

**Backends** (configured with `./config.sh --graphics-backend=<type>`):
- **test** - 16x16 framebuffer, prints hex dump on flush (for testing)
- **virtio** - VirtIO GPU for RISC-V/QEMU (1024×700, hardware accelerated)
- **wasm** - WebAssembly backend (1024×700, works in browser and Node.js)

**Pixel format**: 32-bit BGRA (`0xAARRGGBB`)

**IPC API** (see `services/graphics.yml`):
```cpp
#include "ot/user/gen/graphics-client.hpp"

GraphicsClient client(graphics_pid);
auto fb = client.get_framebuffer();  // Returns {fb_ptr, width, height}
uint32_t *pixels = (uint32_t *)fb.value().fb_ptr;
pixels[y * width + x] = 0xFF0000FF;  // Blue pixel
client.flush();  // Display framebuffer
```

**Building**:
```bash
./build-wasm-graphics.sh           # WASM with graphics
./config.sh --graphics-backend=virtio && ./compile-riscv.sh  # RISC-V with VirtIO
```

**WASM platforms**:
- Node.js with SDL: Install `@kmamal/sdl` for native window (zero-copy WASM memory access)
- Node.js headless: Graphics operations succeed but produce no output

**Frame rate management**:

Use `graphics::FrameManager` for consistent frame timing with cooperative yielding:

```cpp
#include "ot/user/graphics/frame-manager.hpp"

graphics::FrameManager fm(30);  // Target 30 FPS

while (running) {
  if (fm.begin_frame()) {
    // Render only when enough time has passed
    draw_scene();
    client.flush();
    fm.end_frame();
  }
  ou_yield();  // Always yield for cooperation
}
```

The frame manager automatically skips rendering when the process is yielded back too quickly, preventing wasted work.

## Testing

### Unit tests

There are some unit tests with doctest that run on the host system; these can be run with
`./test-unit.sh`.
 
### Snapshot tests

The kernel uses snapshot testing to verify behavior. Tests are defined in `test-snapshot.py` and run
specific kernel configurations by compiling with test flags.

#### Running tests

```bash
# Run all tests
./test-snapshot.py

# Update snapshots after making changes
./test-snapshot.py --update
```

#### Test infrastructure

- `compile-riscv.sh` - Compiles the kernel with optional test flags (e.g., `--test-hello`)
- `test-snapshot.py` - Snapshot test runner that captures TEST: output lines
- `snapshots/` - Directory containing expected output for each test

#### Available test modes

Tests are defined by kernel compile flags:

- `KERNEL_PROG_TEST_HELLO` - Runs a simple hello world process that prints and exits
- Default (no flag) - Runs the shell program

#### Adding new tests

1. Add a new test mode in `otk/kernel.cpp` using `#ifdef` directives
2. Add the test to `TEST_PROGRAMS` dict in `test-snapshot.py`
3. Run `./test-snapshot.py --update` to create the snapshot
4. Test output lines must start with `TEST:` to be captured

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
