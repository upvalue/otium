# ✨ Manse guide

Manse is a development environment written in Otium. Today it is a terminal workspace: one window holding several terminals side by side in a row of columns that scrolls sideways. Ghostty's terminal library, libghostty-vt, handles the terminal emulation. Otium's UI library does the drawing. Manse is built so you can change it while it runs.

This guide covers running Manse, how its files fit together, and how to change it. For the language itself, see the [Otium language guide](../language-guide.md).

## What works today

- One window with a row of terminal columns. It opens two terminals by default.
- Each terminal runs your shell (`$SHELL`, or `/bin/sh`) on its own pseudo-terminal (PTY), with `TERM=xterm-256color`.
- Colours, the cursor, resizing, and keys encoded the way Ghostty encodes them. Typed text comes from SDL's text input.
- Keys to add, close, focus, move and resize columns, and to jump to a column by typing its letters (like Vimium).
- A help panel that lists the keys.
- A config file at `~/.manse/config.ot`.
- A column closes by itself when its shell exits with status 0. If the shell exits with any other status, the column stays open and its header says "exited".

Limits:

- Manse runs on macOS only. The PTY code uses macOS constants, `libSystem.B.dylib`, `_NSGetEnviron`, and `.dylib` paths.
- It runs on the Chez Scheme runtime. The tests are registered for Chez only.
- Terminals are the only kind of app. The app interface is generic, but nothing else implements it yet.

## Building and running

### What you need

- Otium's UI build. Run `just build`. This configures Meson in `build/` and builds the C runtime, the UI library and native helpers. The UI part needs SDL3, SDL3_ttf and `glslc`. It writes `build/ui-config.ot`, which Manse passes to `ui/open`.
- `libmanse-native`. Meson builds it from `manse/native/meson.build`, and only when SDL3 is found. It has two C files:
  - `manse/native/pty.c` starts a child process on a PTY and resizes the PTY. Starting the child needs `setsid` and `TIOCSCTTY` between `fork` and `exec`, and the FFI cannot call variadic `ioctl`.
  - `manse/native/reader.c` reads PTY output on its own thread into a ring of 64 KiB batches. When output arrives it pushes an SDL event, which wakes the UI loop.
- libghostty-vt. Install it with Nix, from Ghostty's flake. Otium's build does not compile Ghostty and has no Zig step. Manse finds the library at runtime.

### How the libraries are found

`manse.ghostty/library-candidates` in `manse/ghostty.ot` tries these in order:

1. `MANSE_GHOSTTY_LIBRARY`, if it is set. Nothing else is tried after it.
2. `libghostty-vt.dylib` and `libghostty-vt.so` by name, through the system loader.
3. `~/.nix-profile/lib/libghostty-vt.dylib`.
4. `/run/current-system/sw/lib/libghostty-vt.dylib`.

If the library is a debug build, Manse prints a warning on stderr. A debug build will be slow.

`manse.pty/native-candidates` in `manse/pty.ot` uses `MANSE_NATIVE_LIBRARY` if it is set. Otherwise it tries `../build/manse/native/libmanse-native.dylib`, then `build/manse/native/libmanse-native.dylib`. These paths work when you run from `manse/` or from the repository root.

### Running

From `manse/`:

```sh
../otium app.ot
```

`manse/project.ot` adds the parent directory to the load path, so `manse.*` modules resolve. Otium finds `project.ot` by walking up from the current directory.

From the repository root, add the load path yourself:

```sh
./otium --runtime=chez -L . manse/app.ot
```

`manse/app.ot` just calls `manse.workspace/main`. That function loads settings, opens the first terminals and the window, and runs the UI loop. If the window is already open, calling it again does nothing.

### Settings

`manse/settings.ot` holds the settings table, `manse.settings/options`. At startup, `main` evaluates `~/.manse/config.ot` if the file exists. Change settings from that file with `configure!`:

```lisp
(require manse.settings :as settings)
(settings/configure! {:font-size 16 :terminals 3 :shell "/bin/zsh"})
```

| Setting | Default | Notes |
| --- | --- | --- |
| `:font-path` | `#f` (the UI's mono font) | A path, a `~/` path, or the filename or stem of an installed font |
| `:font-size` | 14 | 1–256 |
| `:padding` | 6 | Space around the terminal grid, in pixels |
| `:width`, `:height` | 900, 800 | Window size at startup |
| `:terminals` | 2 | Terminals opened at startup, 1–16 |
| `:poll-ms`, `:background-poll-ms` | 16, 100 | UI loop poll intervals, in milliseconds |
| `:shell` | unset | Falls back to `$SHELL`, then `/bin/sh` |

`configure!` checks every value before it changes any. An unknown key or an out-of-range value raises an error and leaves the settings as they were. Font path, font size and padding take effect on the next frame. The other settings only apply at startup. `options` is a `defonce`, so reloading `manse/settings.ot` keeps the current values.

`manse/fonts.ot` turns a font name into a path. It searches `~/Library/Fonts`, `~/.fonts`, the XDG data directories, `/Library/Fonts` and `/System/Library/Fonts` for `.ttf`, `.otf`, `.ttc` and `.otc` files. If a name matches more than one file, you get an error that lists the paths.

### Keys

These are defined in `manse.workspace/bindings`:

| Key | Action |
| --- | --- |
| Cmd+T | New terminal to the right |
| Cmd+W | Close the focused column |
| Cmd+Left / Cmd+Right | Focus the column to the left / right |
| Cmd+Shift+Left / Cmd+Shift+Right | Move the column left / right |
| Cmd+R / Cmd+Shift+R | Widen / narrow the column (1/3, 1/2, 2/3, full) |
| Cmd+J | Jump: each column gets a letter label; type it to focus that column. Escape cancels. |
| Cmd+? | Show or hide the key help |

Inside a terminal, Alt+Left, Alt+Right and Alt+Backspace send `ESC b`, `ESC f` and `ESC DEL`. This happens whatever keyboard mode the terminal is in.

## How it is put together

All Manse modules live under `manse/` as `manse.*` namespaces.

| File | Owns |
| --- | --- |
| `manse/app.ot` | Entry point. Calls `workspace/main`. |
| `manse/workspace.ot` | The workspace state, the window, the view, key bindings and actions, the per-frame step, and scripted runs |
| `manse/apps.ot` | The `App` interface and the registry that gives each app an ID |
| `manse/strip.ot` | Column order, widths, focus and jump labels. Plain data with no UI code. |
| `manse/terminal.ot` | `TerminalApp`, and the `Terminal` widget that measures cells, fits the grid, paints and handles input |
| `manse/session.ot` | A session: one Ghostty terminal joined to one PTY. Passes PTY output to the terminal. |
| `manse/pty.ot` | Opening PTYs, starting children, reading, writing, resizing and reaping, using the FFI and `libmanse-native` |
| `manse/ghostty.ot` | FFI bindings to libghostty-vt: terminals, render state, row and cell iterators, the key encoder, colours |
| `manse/keyboard.ot` | Turns SDL key events into Ghostty key codes and modifiers |
| `manse/widgets.ot` | Small widgets: status dots, jump badges, key caps, spacers, labels |
| `manse/theme.ot` | Colours (after Zed's One Dark) and the UI text size. Also sets `otium.ui`'s palette. |
| `manse/fonts.ot` | Finds and resolves font files |
| `manse/settings.ot` | Settings and the config file |
| `manse/native/pty.c`, `manse/native/reader.c` | The native helpers described above |

### The workspace

`manse.workspace/workspace` is a `defonce` table. It holds the app registry (`:apps`), the strip (`:strip`), the window, the shell, and a few flags. Because it is a `defonce`, reloading `manse/workspace.ot` replaces the code and keeps the running terminals.

`main` opens the window with `ui/open` and passes it three hooks:

- `:step!` runs before each frame. It reaps exited children, runs any scripted input, calls `apps/step!` on every app, and closes apps that report `finished?`.
- `view` builds the UI tree. Each column has a header (status dot, name, width or jump label) above the app's own view. The columns sit in a horizontal `ui/scroll` that scrolls to the focused column.
- `:after-frame!` closes the window when a scripted run is done. It also clears a flag that stops Cmd+W from closing the whole window on macOS.

Useful functions while the program runs: `add-app!`, `close-app!`, `new-terminal!`, `query-apps`, `find-app` and `terminal-sessions`.

### Apps

`manse/apps.ot` defines the abstract class `App` and the generics `info`, `step!`, `close!`, `view` and `finished?`. The registry hands out IDs that never change. `query` returns each app's `info` table with `:id` and `:name` added. `remove!` calls `close!` before it unregisters the app. The strip refers to apps only by ID.

To add a new kind of column, write a class that `:is apps/App` and implement those generics. Then register an instance with `workspace/add-app!`.

### Terminals, sessions and the PTY

A `TerminalApp` wraps one session. `manse.session/open!` creates the Ghostty terminal with `g/new` and starts the shell with `pty/spawn`. Ghostty sometimes needs to send bytes back to the program, such as a cursor position report. Those bytes go back down the PTY through the terminal's `:on-reply` handler.

Each frame, `session/pump!` takes batches of output from the reader thread and feeds them to `g/write-bytes!`. It stops after 8 ms (`parse-budget`). If output is left over, it calls `pty/wake!` so the next frame comes straight away. A burst of output therefore cannot hold up painting. After parsing, it calls `g/update!` to refresh Ghostty's render state, and checks whether the child has exited.

The `Terminal` widget in `manse/terminal.ot` does the painting:

- It measures the cell size by shaping ten `M` characters in the current font. It measures again when the font, size or display scale changes.
- `fit!` resizes the Ghostty grid and the PTY to fit the column. While a column is still changing width, the old grid is redrawn at its old size. The resize happens once, when the width settles, so the shell only redraws once.
- Painting uses two layers of `gfx/cached`. An outer cache redraws an idle terminal without repainting it. Per-row caches repaint only the rows Ghostty marks dirty. Each row is painted as background rectangles plus one text run for each stretch of cells with the same colour.
- Every 120 frames it prints the average paint time per frame to stderr.

Key presses go through `keyboard/encode`, which calls `g/encode-key` so Ghostty's encoder applies the terminal's current modes. Plain text comes in as `:text-input` events and is written to the PTY directly.

### The Ghostty bindings

`manse/ghostty.ot` declares the C struct layouts it uses. `check-layouts!` compares them against the library's own `ghostty_type_json` output, so a mismatch with the installed headers shows up as an error rather than as corrupt memory.

The bindings share one scratch buffer for output parameters. They allocate the key encoder and the UTF-8 buffers on first use, so terminals opened before a reload pick them up too.

For tests there are `grid` and `text`. They allocate per cell, so painting uses `each-row` and `each-row-cell` instead.

## Changing Manse while it runs

Start Manse with the development server turned on:

```sh
./otium --runtime=chez --dev -L . manse/app.ot
```

Then use `tools/otium-dev` from the repository root. The otium-dev skill in `.agents/skills/otium-dev/SKILL.md` describes the tool, and `specs/dev-server.md` describes the protocol. The usual loop is to edit a file and load it:

```sh
tools/otium-dev ls
tools/otium-dev load manse/theme.ot
tools/otium-dev eval '(manse.workspace/query-apps)'
tools/otium-dev eval --ns manse.workspace '(new-terminal!)'
tools/otium-dev capture /tmp/manse.png
```

What survives a reload, and why:

- The workspace, the settings and the PTYs waiting to be reaped are `defonce`. Reloading their modules keeps open terminals running.
- The theme colours are plain `def`s. Loading `manse/theme.ot` restyles the window and also updates `otium.ui`'s palette.
- Ghostty's `write_pty` callback calls back into the namespace instead of holding on to an old function. A reloaded `on-write-pty` therefore applies to terminals that are already open.
- Methods such as `ui/paint` on `Terminal`, and the `view` and `step!` hooks, all go through the namespace. Reloading `manse/terminal.ot` or `manse/workspace.ot` takes effect on the next frame.
- Settings changed with `configure!` apply on the next frame for font and padding.

A load only runs the file's forms. If the next frame fails while painting or building the view, the load does not report it. Check `tools/otium-dev events` for `frame-error`, or take a capture.

Remember that Manse is running your shells. Code you evaluate can type into them or close them.

### Scripted runs

`manse.workspace/scripted!` reads environment variables, which is useful for screenshots and quick checks:

- `MANSE_TYPE` and `MANSE_TYPE_2` type text into terminals 1 and 2 at frame 30.
- `MANSE_KEYS` runs actions by key name, separated by spaces, at frame 40. For example `MANSE_KEYS="Meta+T Meta+R"`.
- `MANSE_FRAMES=N` saves a capture to `MANSE_CAPTURE` (default `/tmp/manse.png`) on frame N−1 and quits at frame N.

## Tests

Run the Manse tests registered with Meson:

```sh
just test manse
```

These tests are listed in `tests/manse/tests.py`: `apps`, `strip`, `settings`, `fonts`, `fit`, `theme`, `pty-write`, `pty-reap`, `construct-cleanup`, `pty-construct-cleanup`, `ghostty-cleanup` and `grapheme-capacity`. They run on Chez with the FFI. They do not need a display or libghostty-vt: where a test reaches native code, it swaps in stand-in functions to test the error and cleanup paths.

The other files in `manse/tests/` need libghostty-vt, `libmanse-native`, real PTYs and, for painting, the headless UI. They are not registered with Meson, so run them by hand from the repository root:

```sh
./otium --runtime=chez -L . manse/tests/ghostty.ot
```

The same goes for `keyboard.ot`, `paint-cells.ot`, `paint-rows.ot`, `pty.ot` and `workspace.ot`.

## Benchmarks

Both benchmarks open a headless window and read `build/ui-config.ot`, so run them from the repository root after `just build`.

`manse/bench.ot` measures frame cost with four busy terminals. It reports full frames split into reconcile, layout, paint and submit; paint time with one terminal updating; scroll animation between columns; and column width changes, including how many times the PTYs were resized:

```sh
./otium --runtime=chez -L . manse/bench.ot
MANSE_BENCH_UNCACHED=1 ./otium --runtime=chez -L . manse/bench.ot
```

`MANSE_BENCH_UNCACHED=1` paints every terminal on every frame, so you can compare with the caches turned off.

`manse/bench-paint.ot` times paint and GPU submission for one terminal with fixed contents. It covers an unchanged screen, typing, cursor movement and scrolling. `W` and `H` set the window size in pixels. `W=1700 H=1125` gives a 200×60 grid, which is the size to judge performance at:

```sh
W=1700 H=1125 ./otium --runtime=chez -L . manse/bench-paint.ot
```
