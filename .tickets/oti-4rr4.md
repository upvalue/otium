---
id: oti-4rr4
status: open
deps: []
links: []
created: 2026-10-30T02:23:15Z
type: feature
priority: 3
assignee: Phil
tags: [manse, ui, macos]
---
# Draw Manse's top strip into the macOS title bar

Make the content view fill the whole window on macOS and keep the native traffic lights, as Chrome and WezTerm do, so Manse can draw its top strip where the title bar is.

Public AppKit calls on the NSWindow, which lib/otium/ui/window.ot already gets from SDL.window.cocoa.window and messages through otium.objc:

- styleMask |= NSWindowStyleMaskFullSizeContentView (1 << 15)
- setTitlebarAppearsTransparent: YES
- setTitleVisibility: NSWindowTitleHidden (1)

What research turned up (2026-10-29):

- WezTerm's INTEGRATED_BUTTONS mode on macOS does exactly this and draws clickable tabs in the title bar area (wezterm window/src/os/macos/window.rs, apply_decorations_to_window). It leaves the traffic lights where AppKit puts them.
- SDL3's Cocoa backend (src/video/cocoa/SDL_cocoawindow.m) works out the window size from contentRectForFrameRect: and [contentView frame], and drops clicks outside the content view's frame. Both follow the style mask, so the size and clicks in the title bar area should be right without changes.
- SDL_SetWindowHitTest works on macOS: returning SDL_HITTEST_DRAGGABLE makes SDL toggle setMovableByWindowBackground:. Use it so empty parts of the strip drag the window and tabs stay clickable. Needs a binding in lib/otium/sdl.ot plus a callback.
- Left inset for the traffic lights: read it from standardWindowButton: frames instead of hard-coding ~70pt.
- Taller strip with the lights centred (as Chrome does with private hooks): probably doable with public API by attaching an empty NSToolbar and setting toolbarStyle to unified or unifiedCompact. Not tested.

Problems others hit:

- The transparent title bar turned grey after a resize or a focus change (Apple forums thread 814858). Fix suggested there: set it again in windowDidBecomeKey.
- Zed made the title bar opaque in fullscreen after it showed up transparent there.
- Traffic lights out of line in fullscreen on macOS 26.0, reportedly fixed in 26.1.

Start with the three calls plus the inset to check that SDL handles the size change, then add the hit test once the strip has clickable things.

## Acceptance Criteria

On macOS, Manse draws its strip from the top edge of the window with the native traffic lights showing and working. Window size and mouse coordinates are correct. Empty parts of the strip drag the window, and anything clickable in the strip still takes clicks. Fullscreen and resizing still behave.

