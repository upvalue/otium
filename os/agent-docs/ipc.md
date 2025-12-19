# Inter-Process Communication (IPC)

The kernel provides a synchronous IPC mechanism for request-reply communication between processes.

## Data Structures

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

## Syscalls

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

## Out-of-Band Data Transfer

For methods requiring more than 3 arguments or complex data:

1. **Sender** writes messagepack data to its comm page using `CommWriter` or `MPackWriter`
2. **Sender** calls `ou_ipc_send()` with `IPC_FLAG_HAS_COMM_DATA` set
3. **Kernel** copies sender's comm page (4KB) to receiver's comm page
4. **Receiver** reads messagepack data from its comm page using `MPackReader`

This allows transmitting arbitrary structured data while keeping simple calls fast with inline arguments.

## Register Optimization

To maximize inline data capacity across different architectures, method and flags are packed into a single field:
- Lower 8 bits: flags (supports up to 256 flag combinations)
- Upper bits: method ID (supports millions of methods on 32-bit, trillions on 64-bit)

This optimization saves one register, allowing:
- **RISC-V**: 3 inline arguments (a2, a4, a5) instead of 2
- **ARM Cortex-M33**: 2 inline arguments (r2, r3) instead of 1
- **WASM**: No constraint, but maintains API consistency

The packing/unpacking is handled transparently at the syscall boundary with soft assertions to warn if method IDs accidentally use the lower 8 bits.

## Scheduling Behavior

IPC operations use immediate scheduling (`process_switch_to`) rather than cooperative scheduling:

1. Sender calls `ou_ipc_send` targeting a receiver
2. If receiver is in `IPC_WAIT`, kernel immediately switches to receiver
3. Receiver processes request and calls `ou_ipc_reply`
4. Kernel immediately switches back to sender with response

This bypasses normal round-robin scheduling, allowing synchronous request-reply patterns.

## Platform-Specific Implementation

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

## Error Codes

- `IPC__PID_NOT_FOUND` - Target process doesn't exist or isn't running
- `IPC__METHOD_NOT_KNOWN` - Receiver doesn't recognize the method ID

## Debugging

Enable IPC tracing with `LOG_IPC` config to see IPC operations and context switches.

## Code Generation

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
