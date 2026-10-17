---
id: lan-yrjm
status: closed
deps: []
links: []
created: 2026-10-17T05:10:14Z
type: feature
priority: 2
assignee: Phil
---
# Replace runtime graphics integrations with portable SDL FFI

Implement the shared SDL manifest, bindings, portable acceptance host and demos. Remove C Raylib and the Chez-specific SDL dependency and host after shared validation.

## Acceptance Criteria

Both runtimes pass portable SDL layout/event/render/texture/audio/reload/cleanup checks; default C has no Raylib, SDL or libffi link dependency; obsolete integrations and tests removed; remaining platform validation gaps documented.


## Notes

**2026-10-17T05:21:21Z**

Replaced C Raylib and bespoke Chez SDL with shared SDL FFI manifest/bindings/host. Removed runtime build/launch/dependency wiring and obsolete sources/tests; migrated orbit/particle scenes. make test and accelerated SDL integration pass both runtimes on macOS arm64/Metal; semi and gen C integration, C GC stress acceptance, 15 headless + 21 accelerated checks, watched error/reload pixel checks, partial-startup cleanup. Dummy and separate real-device audio queue consumption pass; audible output not independently observed. Full GC lint was killed in existing otium.c; focused main.c and ot-ffi.c lint pass. Linux remains unverified; old Raylib roguelike retired, not ported. README files untouched.
