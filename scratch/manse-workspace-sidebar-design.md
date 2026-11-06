# Manse workspace sidebar design

Status: implemented on Atlas's workspace-sidebar branch after subsequent user authorization.
Implementation base: 397247cc5036e0f80e761c7763a7674c66999f4f.
The sections below retain the original design rationale. The implementation uses
separate browser and REPL apps already on main, command-palette workspace actions,
checked Ghostty title constants, and descendant-aware focus restoration.

Verification includes pure membership tests, headless interaction/reload tests,
real external and in-process PTY lifecycle tests, title-only output tests, and
native 900x640 / 480x640 layout checks with captures. Linux PTY integration needs
a temporary adapter for the existing macOS-only libc loader; it is not shipped.
The full pre-existing program/REPL suites are not clean on this host: the program
backpressure assertion also fails on unchanged main, and the REPL interrupt-message
assertion is intermittent on Atlas. No runtime or PTY portability changes are made.
Base inspected: 02114b8c3562b4292ca7c69bd77eb0c6abb5fb5e.

## Confirmed interaction

Workspaces roughly correspond to active projects and group app instances, with
room for future project information. The user confirmed that vertical sidebar
tabs preserve the existing side-by-side columns; they do not replace them.

Show expandable workspace headings with ordered app tabs beneath them. Selecting
a workspace restores its strip and last-focused app. Selecting an app activates
its workspace, focuses its column, and scrolls that column into view. Sidebar
order matches column order. Preserve column navigation, sizing, and rearrangement.
Titles come from app metadata, including dynamic terminal titles, falling back to
the registration name. Identity never depends on titles.

## Architecture and ownership

- Keep `manse/workspace.ot` as the window coordinator, avoiding a namespace rename.
  Add `manse/workspaces.ot` for the pure collection model and `manse/sidebar.ot`
  for rendering and sidebar interaction.
- Keep one global `manse.apps` registry owning app lifetime and stable app IDs.
  Each workspace has a distinct stable ID, name, its own `manse.strip` state,
  and expansion state. The strip's ordered app IDs are authoritative membership;
  do not duplicate this as another ordered array. Each app belongs to one workspace.
- Store active workspace ID in the collection. Each strip already stores its
  focused app, column widths, order, and jump hints.
- Insert new apps after the focused app in the active workspace. Moving an app
  transfers its existing strip item and width, preserves its ID and instance,
  and repairs source focus. Reordering changes strip order. Begin with explicit
  commands; drag-and-drop can follow.
- Continue stepping every registered app, including inactive workspaces.
  Switching, collapsing, moving, and unmounting views never unregister apps or
  terminate processes. Preserve inactive terminal dimensions until displayed.
  App/controller state that must survive remounting belongs outside widget nodes.

Existing foundations: `manse/apps.ot:27` registers stable IDs and `:54` closes on
removal; `manse/strip.ot:16` stores layout state; `manse/workspace.ot:133` steps
all apps; `lib/otium/ui.ot:480` retires view nodes without closing app instances.

## Titles, focus, and layout

`manse/terminal.ot:26` currently exposes status but no title. Add an optional
`:title` in `apps/info`, shared by sidebar and column headers. The installed
Ghostty API exposes `GHOSTTY_TERMINAL_DATA_TITLE = 12` as a borrowed `GhosttyString`.
`manse/ghostty.ot` already defines GString (:138), terminal_get (:185), and a
counted UTF-8 decoder (:276). Add a narrow wrapper with correctly sized GString
storage; the scalar scratch buffer is insufficient. Copy borrowed bytes before
another terminal mutation. Cache title after pumping and invalidate on title-only
OSC changes. No Otium runtime changes are needed.

Scope content-focus detection explicitly to the content strip. The current
`workspace.ot:169` heuristic recognizes children of any Scroll and would confuse
sidebar rows with columns. Sidebar keys must be distinct from content keys.
Prevent automatic content-focus restoration from stealing deliberate sidebar
focus; clear stale requested/UI focus tracking when switching. Cancel jump hints
on workspace switches.

Preserve existing column shortcuts. Add workspace navigation and sidebar-focus
commands, with exact bindings chosen during implementation. Within the sidebar,
arrows navigate rows, Enter activates content, and Escape dismisses menus/drawers;
these events must not leak into terminal input. Compute column fractions from
content width after subtracting sidebar width. On narrow windows, collapse to an
overlay drawer with an always-available workspace toggle; selection dismisses it.

## Proposed first-version defaults

These are recommendations, not additional user decisions:

- Start with one empty workspace and no terminals, matching the current zero-
  terminal default. Empty workspaces offer app creation. Launch failures leave
  them usable; future asynchronous launches need explicit loading/error states.
- Explicit Close app retains existing termination semantics. Switching never
  closes. Retain current successful-terminal-exit removal policy initially.
- Permit deleting empty workspaces only. Apps can be moved out first. Preserve
  at least one empty workspace when deleting the last one. A future bulk close
  action must explicitly communicate that it terminates contained apps.
- Preserve the existing macOS Cmd+W/window-close guard.
- Preserve live-process state through defonce and an idempotent migration from
  the old single strip into the initial workspace. Do not reset live registries.
- Do not implement restart persistence initially. `settings.ot` evaluates user
  configuration; `ui-config.ot` loads native library settings, not session state.
  Later restoration needs versioned data and launch descriptors, not serialized
  live apps or an implication that saved IDs restore PTYs.
- Future optional project root metadata must not determine workspace identity.
  Do not introduce a project framework. The shell launch API currently lacks a
  cwd argument, so a root field alone must not promise launch-directory behavior.

## Implementation sequence and verification

Implemented sequence: collection model/invariants and live-state
migration; coordinator integration; sidebar/focus routing; terminal titles and
shared label helper; UI validation. No general runtime changes are planned.

Test stable IDs, single ownership, membership/order, active selection, empty and
last workspace behavior, moves without close/restart, inactive app stepping,
background exits, OSC 0/2 titles including empty/Unicode/title-only changes,
focus and terminal input isolation, switching/remounting preserving PID/history,
live reload, narrow layouts, and the macOS close guard. Extend
`manse/tests/workspace.ot`; register pure model tests in `tests/manse/tests.py`.
Use display/PTY integration checks for behaviors the pure suite cannot exercise.

## Coordination

The live-inspector task proposes app implementations under `manse/apps/` while
keeping `apps.ot` shared. This is compatible. Coordinate changes to workspace.ot,
app insertion, terminal imports/tests, and the optional info :title contract.
Inspector state must survive view retirement through its app/controller. Do not
cherry-pick or integrate sibling work as part of this design task.

No further product decision blocks the plan. Exact shortcuts and sizing are
routine implementation choices. Implementation was subsequently authorized and completed; integration remains separate.
