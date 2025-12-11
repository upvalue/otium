# Text Editor

A minimal vim-like text editor for the Otium OS. It runs as a standalone POSIX tool and uses termbox2 for terminal rendering.

## Architecture

```
┌─────────────────────┐
│   edit_run()        │  Core editor logic (platform-agnostic)
│   ot/user/edit.cpp  │
└─────────────────────┘
          │
          ▼
┌─────────────────────┐
│   Backend Interface │  Abstract terminal interface
│   ot/user/edit.hpp  │
└─────────────────────┘
          │
    ┌─────┴─────┐
    ▼           ▼
┌────────┐  ┌────────┐
│Termbox │  │  Test  │
│Backend │  │Backend │
└────────┘  └────────┘
```

## Backends

| Backend | File | Purpose |
|---------|------|---------|
| Termbox | `ot/core/platform/edit-termbox.cpp` | Real terminal via termbox2 library |
| Test | `ot/core/platform/edit-test.cpp` | Scripted input for unit tests |

## Editor Modes

The editor has three modes, similar to vim:

| Mode | Status Line | Description |
|------|-------------|-------------|
| NORMAL | `[normal]` | Navigate, enter other modes |
| NORMAL (operator pending) | `[normal d]` | Waiting for motion after `d` |
| INSERT | `[insert]` | Type text into buffer |
| COMMND | `[commnd]` | Enter Tcl commands (like vim's `:` mode) |

## Key Bindings

### All Modes
- `Ctrl-D` - Page down (half screen)
- `Ctrl-U` - Page up (half screen)
- Arrow keys - Move cursor

### Normal Mode

**Mode changes:**
- `i` - Enter insert mode
- `;` - Enter command mode (note: `;` not `:`)

**Motions:**
- `h` - Move left
- `j` - Move down
- `k` - Move up
- `l` - Move right
- `0` - Move to beginning of line
- `$` - Move to end of line

**Operators:**
- `d` + motion - Delete text covered by motion
  - `d$` - Delete to end of line
  - `d0` - Delete to beginning of line
  - `dd` - Delete entire line

### Insert Mode
- `Esc` - Return to normal mode
- `Enter` - Insert newline
- `Backspace` - Delete character before cursor
- Printable chars - Insert at cursor

### Command Mode
- `Enter` - Execute command
- `Backspace` - Delete character
- `Esc` - (not implemented, use Enter with empty command)

## Commands

Commands are Tcl expressions. Built-in editor commands:

| Command | Aliases | Description |
|---------|---------|-------------|
| `q` | `quit` | Quit (fails if unsaved changes) |
| `q!` | `quit!` | Force quit without saving |
| `w` | `write` | Write file to disk |

All standard Tcl commands (`set`, `if`, `while`, `proc`, etc.) are also available.

## Building

```bash
# Build the editor
meson compile -C build-posix edit

# Run
./build-posix/edit <filename>
```

## Testing

The editor has a test backend that accepts scripted key sequences:

```cpp
#include "ot/user/edit.hpp"
using namespace edit;

// Create key sequence
Key script[] = {
    key_char('i'),           // enter insert mode
    key_char('H'), key_char('i'),
    key_esc(),               // back to normal
};

// Run and get final buffer contents
auto result = edit_test_run(script, sizeof(script)/sizeof(script[0]));
CHECK(result[0] == "Hi");
```

Helper functions for building key sequences:
- `key_char(c)` - Regular character
- `key_ctrl(c)` - Ctrl+character
- `key_esc()`, `key_enter()`, `key_backspace()` - Special keys
- `key_up()`, `key_down()`, `key_left()`, `key_right()` - Arrow keys

Run tests:
```bash
./build-posix/unit-test --test-case="edit*"
```

## Source Files

| File | Purpose |
|------|---------|
| `ot/user/edit.hpp` | Editor interface, Key struct, Backend class |
| `ot/user/edit.cpp` | Core editor logic |
| `ot/core/platform/edit-termbox.cpp` | Termbox2 terminal backend + main() |
| `ot/core/platform/edit-test.cpp` | Test backend + `edit_test_run()` |
| `ot/user/edit-test.cpp` | Unit tests |
| `vendor/termbox2/termbox2.h` | Termbox2 library (single header) |

## Keybinding Architecture

Key handling uses a table-driven approach that separates keybindings from actions:

- **Actions** (`enum class Action`) - What the editor does (move cursor, delete, change mode)
- **Keybindings** (`Keybinding` struct) - Maps a key + mode to an action
- **Operators** (`enum class Operator`) - Operators like `d` that combine with motions

This allows:
- Easy addition of new keybindings
- Future support for configurable keybindings
- Clean separation of input handling from editor logic

## Future Work

Planned vim-like enhancements:
- Word motions (`w`, `b`, `e`)
- More operators (`c` for change, `y` for yank)
- Single-char delete (`x`)
- First non-blank (`^`)
- Yank/paste (`y`, `p`)
- Search (`/`, `n`, `N`)
- Motion counts (`3j`, `d2w`)
