# Portable SDL3 on C and Chez

SDL goes through the FFI, with one shared binding and canvas implementation in
`native/sdl/` and no SDL-specific C wrappers. Build with libffi and SDL3
installed, then generate an installation manifest:

    just build
    just sdl-manifest

SDL3 and pkg-config must be installed for setup. The checked-in macOS arm64
manifest was generated from SDL 3.4.16 headers. It records that version and a
SHA-256 hash of the headers; it does not claim to have probed 3.4.12. The minimum
runtime version remains SDL 3.4.12, within major version 3. All required symbols
are resolved eagerly. Linux x86-64 needs its own generated manifest and native
validation; the macOS result does not establish Linux support.

Setup compiles and runs a temporary C probe. It records the compiler target,
ABI options, scalar sizes/alignments, typedef signedness/representation,
constants, and aggregate sizes, alignments, field offsets and representations.
It rejects cross-target compiler overrides and unexpected field types. There
are no per-function SDL C wrappers. Launching an installed manifest needs
neither a compiler nor headers. The shared binding checks the runtime's layouts
against the manifest before calling SDL.

Regenerate after changing SDL headers or compiler ABI options:

    just sdl-manifest
    just sdl-manifest --check

--check recompiles the probe and compares all metadata with the manifest.
--manifest PATH selects a different output/check file. CC and CFLAGS configure
the native compiler; cross-compilation is not supported. The default path is
native/sdl/manifests/<machine>-<system>.json.

The library search order is SDL3_LIBRARY, then the manifest's library path,
then platform loader names when the manifest has no library path. An explicit
SDL3_LIBRARY failure is an error, with no fallback to a different installation.
The same rule applies on both runtimes.

Run either animated scene:

    python3 examples/run_sdl.py --runtime c --scene shapes
    python3 examples/run_sdl.py --runtime chez --scene particles

The host watches the shared scene file between frames. Saving it reloads the
drawing code; R retries immediately. Escape or closing the window exits. Window,
renderer, scene phase and frame count persist through reload. Syntax errors
leave the old drawing function available. Render errors show a fallback frame
until a successful reload. Scene sources are limited to 1 MiB. Reload is not a
transaction: a failing top-level expression can leave earlier definitions
changed. Keep persistent state in the host, outside the scene source.

Launcher options:

    --runtime c|chez        Select runtime; default c.
    --scene shapes|particles
                           Select shared animation; default shapes.
    --scene-path FILE       Watch this source instead of the selected scene.
    --frames N             Stop after N frames; 0 means run until quit.
    --capture FILE         Save the final frame as BMP before presenting it.
                           Requires --frames, except in acceptance mode.
    --status FILE          Atomically publish numeric status after each frame.
                           Fields, separated by spaces: completed frames, scene
                           ticks, reloads, reload errors, render errors, saved
                           window identity (1/0), saved renderer identity (1/0).
                           Used by the scene host, not acceptance mode.
    --renderer NAME        SDL renderer name/list. Default metal on macOS;
                           opengl,opengles2,vulkan on Linux. Software is rejected.
    --audio off|dummy|real  Queued 48 kHz mono float PCM, with a null callback.
                           Default off. Dummy sets SDL_AUDIO_DRIVER=dummy.
                           Real rejects that environment setting.
    --manifest FILE        Select installation manifest.
    --acceptance           Run the shared render/input/texture/audio checks.
    --inject-error         Raise an Otium error after resources exist. Acceptance
                           mode catches it and checks teardown; scene mode exits
                           with an error after unwinding the session.

The host owns one SDL session on the interpreter's main thread and calls
SDL_SetMainReady before initialization. Cleanup stops text input, releases
native allocations and resources in reverse creation order, then calls SDL_Quit.
Textures precede renderer destruction; the renderer precedes its window.

Tests:

    just test sdl
    just test 'sdl accelerated'

Both modes check the manifest, invalid manifest/override rejection, copied text
whose native bytes change on the next fixture poll, input conversion, and dummy
audio queue bounds and consumption. Graphics checks use a real accelerated
renderer, verify captured background and texture pixels, inject R/Escape/close/
mouse events, reload drawing code while resources persist, and check teardown
after an Otium error. The watched-scene test recovers from syntax and render
errors using frame-status handshakes. Both shared animations run on both hosts.
The `sdl` test, in the native suite, reports an explicit graphics skip if no
display or backend is available. `sdl accelerated`, in the gpu suite, makes that
absence a failure. On macOS, a
sandbox without display-server access cannot run the graphics checks.

Real-device audio is a separate smoke test:

    python3 examples/run_sdl.py --runtime c --acceptance --audio real --inject-error
    python3 examples/run_sdl.py --runtime chez --acceptance --audio real --inject-error

The acceptance test verifies that the device consumes queued PCM after resume,
then refills only below a fixed queue threshold. This is separate from dummy
queue mechanics; listening to the 440 Hz tone is still needed to confirm audible
output. Both paths report SDL_WasInit=0 and zero foreign allocations after
teardown. macOS arm64 passed both real-device commands with Metal and SDL 3.4.16.
