# Manse

Build native helpers with `just build`, then run from `lang/`:

```sh
./otium --runtime=chez --dev -L . manse/app.ot
```

From `lang/manse/`, use `../otium --runtime=chez --dev app.ot`.
Manse needs libghostty-vt; `MANSE_GHOSTTY_LIBRARY` can name its shared library.

## Apps

Manse hosts apps. Each registered app has a stable integer ID and a name;
several apps may share a name. IDs are not reused within a workspace registry.
Each app supplies its own UI. The workspace currently displays all apps in a
vertical stack, with Ctrl+Tab and Ctrl+Shift+Tab moving focus as before.

Query a running workspace through the dev server:

```sh
tools/otium-dev eval --ns manse.workspace '(query-apps)'
tools/otium-dev eval --ns manse.workspace '(query-apps (fn (info) (eq? (info :kind) :terminal)))'
tools/otium-dev eval --ns manse.workspace '(find-app 1)'
```

`query-apps` returns metadata tables in registration order, optionally filtered
by an Otium predicate. Every result includes `:id` and `:name`. Terminal apps
also report `:kind :terminal`, `:status` (`:running`, `:exited`, or `:closed`),
`:pid`, `:tty`, `:cols`, `:rows`, `:exit`, and `:eof?`. `:exit` is `#f` while
running, then a table with `:exit` or `:signal`. `find-app` returns the live
app object, or `nil` for an unknown ID. A terminal app's `:session` slot holds
its PTY session and terminal state.

The contract lives in `manse.apps`; `manse/app.ot` remains the launcher.
Implement an `apps/App` subtype and these generic methods:

- `apps/info` returns app-specific metadata. The registry adds ID and name.
  Return inspection data; copy mutable state that callers should not change.
- `apps/view` returns a UI description. The workspace assigns its root key
  from the app ID and autofocuses the first app.
- `apps/step!` performs bounded background work and returns truthy when the
  app needs repainting. The default does nothing.
- `apps/close!` releases owned resources. The default does nothing.

`(add-app! name app)` registers an app and returns its ID. `(close-app! id)`
closes and unregisters it; an unknown ID is harmless. These workspace functions
invalidate the display. Closing an app also closes the resources it owns,
including a terminal's shell. Exited terminals stay available for inspection;
Manse remains open when the last shell exits or the last app is removed.

Registry state and app objects survive code reloads. Reloading the new
workspace code adopts existing terminal sessions. Terminal drawing and input
now live in `manse/terminal.ot`; reload that file to change terminal behavior.

## Settings

Manse evaluates `~/.manse/config.ot` at startup if the file exists. It does not
create the file. For example:

```scheme
(require manse.settings :as settings)
(settings/configure! {:font-path "Menlo"
                     :font-size 16
                     :padding 8
                     :width 1000
                     :height 800
                     :terminals 1
                     :shell "/bin/zsh"})
```

`:font-path` accepts an explicit file path, filename (`"Menlo.ttc"`), or
filename stem (`"Menlo"`). Filename matching is case-insensitive; it does not
search internal family names. Existing paths take precedence. `~/` expands
to your home directory, and relative paths resolve from Manse's working directory.

Discovery searches `~/Library/Fonts`, `/Library/Fonts`, `/System/Library/Fonts`,
`~/.fonts`, `$XDG_DATA_HOME/fonts` (default `~/.local/share/fonts`), and the
`fonts` subdirectories of `$XDG_DATA_DIRS` (default `/usr/local/share:/usr/share`).
It recurses into subdirectories, ignores absent or unreadable directories, and
accepts `.ttf`, `.otf`, `.ttc`, and `.otc` files. Directory symlinks below a
search root are skipped to avoid cycles; font-file symlinks are included.
Ambiguous names report their matching paths so you can choose explicitly.
Discovery happens when applying settings, and the resolved path is retained.

To inspect matches through the dev server:

```sh
tools/otium-dev eval --ns manse.settings '(fonts/find "Menlo")'
tools/otium-dev eval --ns manse.settings '(fonts/discover)'
```

Font collections use the first face. Choose a monospaced font for terminal cell alignment; Manse
does not enforce this. Set `:font-path #f` to restore bundled JetBrains Mono.
A missing path is rejected by `configure!`; a file that SDL_ttf cannot open
reports a render error. Correct the setting and reload the config to recover.

The native bridge must be rebuilt and Manse restarted once to install font
selection. After that, font changes can be loaded through the dev server.

Defaults are font size 14, padding 6, a 900 by 800 window, two terminals, and
`$SHELL` (falling back to `/bin/sh`). Dimensions and font sizes are logical
pixels. `:terminals` accepts 1–16; font size accepts 1–256. Unknown keys and
invalid values raise an error before any settings in that call are changed.

`:poll-ms` defaults to 16 and `:background-poll-ms` to 100. These control PTY
polling while focused and while unfocused or invisible. SDL input wakes the
loop immediately. Development requests are checked at least every 100 ms
when the loop is otherwise idle. Manse still polls PTYs; it does not yet have
an I/O thread that wakes SDL on output.

## Live development

```sh
tools/otium-dev ls
tools/otium-dev load manse/workspace.ot
tools/otium-dev capture /tmp/manse.png
tools/otium-dev events
tools/otium-dev eval --ns manse.workspace '(settings/configure! {:font-size 18})'
tools/otium-dev eval --ns manse.workspace '(settings/load!)'
```

Pass `--pid N` when several servers are running. Font path, size, and padding change
on the next rendered frame, including recalculation of the terminal grid.
Other settings apply when starting a new workspace. Reloading `workspace.ot`
or `app.ot` preserves the window, shells, terminal contents, and focus.
Settings also survive module reloads. A render error is reported through the
dev server and leaves the window alive so the code can be repaired and loaded.

Manse repaints on terminal output, input, exposure, resize, explicit
invalidation, or a dev-server evaluation or load. It skips painting while
hidden, minimized, or occluded, while continuing to drain PTY output. Captures
force a frame even when the window is invisible.

The shared UI API exposes this behavior with `:render-on-demand? #t` in
`ui/open` and `(ui/invalidate! window)` after model changes. `:step!` and
`:after-frame!` still run on idle iterations; `:frame` counts actual rendered
frames. Existing UI applications retain continuous rendering by default.

## Checks

From `lang/`:

```sh
./otium --runtime=chez -L . manse/tests/fonts.ot
./otium --runtime=chez -L . manse/tests/settings.ot
./otium --runtime=chez -L . manse/tests/ghostty.ot
./otium --runtime=chez -L . manse/tests/pty.ot
./otium --runtime=chez -L . manse/tests/apps.ot
./otium --runtime=chez -L . manse/tests/workspace.ot
meson test -C build --setup all 'ui demand chez' 'ui demand c'
```

The UI accepts font paths wherever it accepts `:ui` or `:mono`, including
`gfx/text`, labels, and caret measurement. Custom paths are registered once
per spelling, with a limit of 32 files and 64 total font/size pairs. Switching
back reuses existing entries. Fonts remain alive while renderers can reference
their glyphs; the registry is released at UI teardown.

Text runs use a fixed 128-slot cache. Each renderer has one fixed glyph atlas;
fonts and runs are released after the last renderer and layout node close.
Manse converts codepoint lists directly to strings rather than retaining every
sequence it has seen. RSS can remain above its starting value after output:
terminal history, the managed heap, and GPU allocations all affect it. Compare
repeated runs after warming the caches before treating RSS growth as a leak.
