A minimal C++ TCL interpreter implementation with modular command registration, unit testing support, and POSIX shell compatibility.

# TCL Interpreter Component

The TCL (Tool Command Language) interpreter in the Otium OS is a lightweight, dependency-minimal implementation designed for embedded environments. It provides a flexible scripting interface for system shells and automation tasks.

## Architecture Overview

The interpreter consists of several key components:

- **Parser** (`tcl::Parser`) - Tokenizes and parses TCL scripts
- **Interpreter** (`tcl::Interp`) - Main execution engine that evaluates commands
- **Commands** (`tcl::Cmd`) - Individual command implementations
- **Variables** (`tcl::Var`) - Variable storage and scoping
- **Call Frames** (`tcl::CallFrame`) - Function call stack management

## Key Files

The TCL interpreter implementation spans several files:

### Core Implementation
- `/ot/user/tcl.hpp` - Main header with class definitions
- `/ot/user/tcl.cpp` - Core interpreter implementation
- `/ot/user/tcl-test.cpp` - Unit tests using doctest framework

### Shell Integration
- `/ot/user/prog/shell/commands.hpp` - Shell command declarations
- `/ot/user/prog/shell/commands.cpp` - Shared shell command implementations
- `/ot/user/prog/shell/textshell.cpp` - POSIX-compatible text shell
- `/ot/user/prog/shell/uishell.cpp` - UI-based shell variant

### Code Generation
- `/ot/user/gen/tcl-vars.hpp` - Auto-generated TCL variable definitions
- `/tools/ipc-codegen/templates/tcl-vars.eta` - Template for variable generation

## Adding a Basic Command

To add a new command to the TCL interpreter, follow these steps:

### 1. Define the Command Function

Create a command function following the standard signature:

```cpp
tcl::Status cmd_mycommand(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
    // Check argument count (command name is argv[0])
    if (!i.arity_check("mycommand", argv, 2, 3)) {
        return tcl::S_ERR;
    }

    // Validate integer arguments if needed
    if (!i.int_check("mycommand", argv, 1)) {
        return tcl::S_ERR;
    }

    // Implement command logic
    int value = atoi(argv[1].c_str());

    // Set result string
    i.result = "command executed successfully";

    return tcl::S_OK;
}
```

### 2. Register the Command

Add the command registration in the appropriate location:

```cpp
// For shell-specific commands, add to register_shell_commands() in commands.cpp:
void register_shell_commands(tcl::Interp &i) {
    // ... existing commands ...

    i.register_command("mycommand", cmd_mycommand, nullptr,
                      "[mycommand arg1:int arg2?:string] => string - My command description");
}

// For core TCL commands, add to register_core_commands() in tcl.cpp
```

### 3. Command Implementation Guidelines

- Use `i.arity_check()` to validate argument counts
- Use `i.int_check()` for integer argument validation
- Set `i.result` to return values
- Return appropriate status codes:
  - `tcl::S_OK` - Success
  - `tcl::S_ERR` - Error
  - `tcl::S_RETURN` - Return from procedure
  - `tcl::S_BREAK` - Break from loop
  - `tcl::S_CONTINUE` - Continue loop

## Unit Testing

The TCL interpreter includes comprehensive unit tests in `tcl-test.cpp` using the doctest framework.

### Running Tests

Tests are built and run as part of the standard build process:

```bash
# Build and run tests
meson test -C build

# Run specific test suite
./build/tcl-test
```

### Writing Tests

Add new test cases to `tcl-test.cpp`:

```cpp
TEST_CASE("tcl - my feature") {
    tcl::Interp i;
    tcl::register_core_commands(i);

    SUBCASE("basic functionality") {
        CHECK(i.eval("mycommand 42") == tcl::S_OK);
        CHECK(i.result.compare("expected result") == 0);
    }

    SUBCASE("error handling") {
        CHECK(i.eval("mycommand invalid") == tcl::S_ERR);
        CHECK(i.result.length() > 0);  // Error message should be set
    }
}
```

### Test Categories

The existing tests cover:
- Basic evaluation and parsing
- Variable operations
- Arithmetic operators (+, -, *, /)
- Comparison operators (==, !=, <, >, <=, >=)
- String operations
- List operations
- Control flow (if, while, proc)
- Command registration and execution

## POSIX Shell Version

The `textshell.cpp` implements a POSIX-compatible text-based shell that can run both interactively and in script mode.

### Features

- **Interactive Mode**: REPL (Read-Eval-Print Loop) for interactive command execution
- **Script Mode**: Execute TCL scripts from files via command-line arguments
- **Startup Script**: Executes `shellrc` on startup for initialization
- **Memory Management**: Uses local storage with configurable page allocation
- **Command History**: Basic command line editing support

### Building for POSIX

When building with `OT_POSIX` defined, the shell provides compatibility shims:

```cpp
#ifdef OT_POSIX
void *ou_malloc(size_t size) { return malloc(size); }
void ou_free(void *ptr) { free(ptr); }
void *ou_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
#endif
```

### Shell-Specific Commands

The text shell adds several built-in commands:
- `quit` - Exit the shell
- `shutdown` - Shutdown the system
- `crash` - Intentionally crash (for testing)
- All file system commands (fs/read, fs/write, fs/ls-dir)
- Process management (run, proc/lookup, proc/is-alive)
- IPC communication (ipc/send)

## MessagePack Integration

The interpreter supports MessagePack for efficient binary data serialization:

```cpp
// Register MessagePack functions
char *mp_buffer = (char *)ou_alloc_page();
i.register_mpack_functions(mp_buffer, OT_PAGE_SIZE);

// This enables commands like:
// mpack/encode, mpack/decode, mpack/list, etc.
```

## Best Practices

1. **Memory Management**: Use the provided `ou::string` and `ou::vector` containers that integrate with the custom allocator
2. **Error Handling**: Always validate inputs and provide meaningful error messages via `i.result`
3. **Documentation**: Include docstrings when registering commands for built-in help
4. **Testing**: Add unit tests for new commands and features
5. **Thread Safety**: The interpreter is not thread-safe; use separate instances per thread

## Example: Complete Command Implementation

Here's a complete example adding a "reverse" string command:

```cpp
// In commands.cpp:

tcl::Status cmd_reverse(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
    if (!i.arity_check("reverse", argv, 2, 2)) {
        return tcl::S_ERR;
    }

    tcl::string result;
    const tcl::string &input = argv[1];

    // Reverse the string
    for (int j = input.length() - 1; j >= 0; j--) {
        result.push_back(input[j]);
    }

    i.result = result;
    return tcl::S_OK;
}

// In register_shell_commands():
i.register_command("reverse", cmd_reverse, nullptr,
                  "[reverse str:string] => string - Reverse a string");

// In tcl-test.cpp:
TEST_CASE("tcl - reverse command") {
    tcl::Interp i;
    tcl::register_core_commands(i);
    shell::register_shell_commands(i);

    SUBCASE("basic reversal") {
        CHECK(i.eval("reverse hello") == tcl::S_OK);
        CHECK(i.result.compare("olleh") == 0);
    }

    SUBCASE("empty string") {
        CHECK(i.eval("reverse \"\"") == tcl::S_OK);
        CHECK(i.result.compare("") == 0);
    }
}
```

This implementation demonstrates the complete workflow for adding functionality to the TCL interpreter.