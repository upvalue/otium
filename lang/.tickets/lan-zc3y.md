---
id: lan-zc3y
status: closed
deps: []
links: []
created: 2026-08-16T02:02:05Z
type: feature
priority: 2
assignee: Phil
tags: [extensions, runtime, design]
---
# Extension system: Foreign userdata, native modules via require, ext/ add-ons (Raylib as worked example)

Mechanism for extending otium with optional 3rd-party native add-ons (graphics, files, etc.) without growing the low-memory core. Three pieces: (1) a Foreign heap object (userdata equivalent), (2) a native-module registry hooked into require, (3) build-time-optional extensions living in ext/ and linked into the executable, never into libotium. Raylib is the design stress test and first worked example (not necessarily implemented with this ticket).

Settled in discussion (2026-08-15):
- Foreign supports BOTH inline-POD and external-pointer payload modes from day one. Inline for small by-value handles (raylib Texture2D etc. — Cheney memcpy moves them safely); pointer mode for owned external resources (file handles, sqlite conns), carrying the finalizer.
- require pre-switches into the module namespace before calling a native init fn and restores after (same save/restore that already wraps source loads in require_load, eval.cpp). Extension init is zero-boilerplate; it may still ns_switch mid-init for sub-namespaces since the harness restores regardless.
- Companion .scm sugar layer loads externally from the load path (not embedded like the prelude). Sequencing: native-registry hit runs init first, then falls through to the normal source load if <name>.scm exists, so the sugar can refer the natives it wraps.
- No dlopen: static linking only, selected via meson options (fits embedded focus and -fno-exceptions/-fno-rtti posture).

## Design

FOREIGN (libotium):
- New Tag::Foreign + ObjType::Foreign. One tag total, NOT one per extension type.
- ForeignData: { u32 typeId; u32 flags (bit 0 = dead/released); payload inline OR external void* }.
- Per-VM foreign-type registry: ForeignType { u32 nameSym (e.g. 'raylib/texture — printer + type checks); void (*finalize)(Vm&, void*) or null }.
- Finalizers join the existing Heap::finalizable vec (same mechanism as Array/Table/Buffer C-heap ownership).
- v0 restriction: foreign payloads may NOT contain Values — no per-type trace hook until an extension demands one. Workaround: companion table keyed by the foreign object.
- Mechanical fallout: is_heap range check in value.hpp; printer case (#<raylib/texture>); val_eq / equal? / val_hash by identity via identityOf.

NATIVE MODULE REGISTRY (libotium):
- Per-VM registry name -> void (*init)(Vm&). require checks it BEFORE loadFn.
- On hit: ns_get_or_create + switch to module name (otium.core auto-referred), run init, restore currentNs, then attempt normal source load of <name>.scm as the optional sugar layer.
- On miss: existing source path unchanged. Missing module = same module-not-found condition as today, so programs can feature-test via handlers.
- def_native (builtins/sys.cpp) already defines into currentNs; comment says otium.core — fix comment, likely no code change needed inside pre-switched init.

BUILD / LAYOUT:
- ext/<name>/<name>_ext.cpp + optional ext/<name>/<name>.scm.
- meson_options.txt: option('ext_raylib', boolean, false) etc. Extension links into the executable (or ext_ static lib). libotium gains only the generic hooks (Foreign + two registries).
- repl/main.cpp registers compiled-in modules under #ifdef.
- Extension ABI = the existing Slot/Scope/ARG GC discipline. No stable C ABI yet (deliberately punted).

RAYLIB GAME-OUT (informs the design):
- Immediate-mode poll-based: main loop lives in otium; natives are thin leaves; no C->eval reentrancy; interruptFlag keeps ^C working per frame.
- Handles (Texture2D, Font, RenderTexture2D) = inline Foreign payloads. Window is global — just functions.
- Vector2/Color/Rectangle are NOT userdata: flat args + packed 0xRRGGBBAA int colors; (rgb r g b) sugar in .scm layer. Keeps per-frame allocation at zero on a 4 MiB heap.
- Resource lifetime sharp edge: UnloadTexture needs a live GL context. Primary mechanism is explicit (unload-texture t) setting the dead flag (later use raises a condition, not a crash); GC finalizer is best-effort backstop that no-ops if the window is gone.
- Enums/keys as constants in the .scm layer.

PUNTED: stable C ABI for out-of-tree extensions; Values-in-userdata/trace hooks; threading.

## Acceptance Criteria

- Tag::Foreign objects can be minted from an extension with both inline and pointer payloads; finalizers run at collection/teardown; dead-flag use raises a condition.
- Printer, eq?/equal?, hash handle Foreign sensibly (identity, #<type-name> repr).
- (require 'somemod) resolves a registered native module: init runs inside the module's namespace, currentNs restored after, companion .scm loaded from the load path afterwards when present.
- Unregistered native module falls through to source loading unchanged; missing entirely -> module-not-found condition.
- A demo extension under ext/ behind a meson option builds when enabled, is absent when disabled, and libotium itself contains no extension-specific code.
- def_native comment corrected.

## Notes

**2026-08-16T02:35:19Z**

Implemented Foreign inline and pointer payloads, collection and teardown finalization, identity printing/equality/hashing, native require sequencing, optional demo and Raylib extensions, companion Scheme layers, tests, docs, and a Raylib example. Verified the default build (2 tests) and demo+Raylib build against local Raylib 6.0 (4 tests); formatting and diff checks pass.

**2026-08-16T02:50:26Z**

Expanding the Raylib surface with the grid text, input, scaling, window, timing, and RNG primitives needed for a small Brogue-like roguelike, plus a playable example.

**2026-08-16T02:56:42Z**

Added the roguelike-ready Raylib slice: allocation-free codepoint drawing, default and owned font handles, measurement, texture-region and scaled render-target drawing, resize/fullscreen controls, scissoring and line primitives, key and Unicode queues, fuller mouse input, timing, and seeded integer RNG. Added a parsed 60x30 playable roguelike example. Default tests pass 2/2 and extension-enabled tests pass 4/4 with Raylib 6.0; formatting and diff checks pass.
