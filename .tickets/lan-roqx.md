---
id: lan-roqx
status: open
deps: []
links: []
created: 2026-10-19T04:52:12Z
type: task
priority: 2
assignee: Phil
---
# Retire remaining Python test drivers and scripts

Left in Python during the 2026-10-18 build/test cleanup because Otium lacks a feature, or deferred for size. All are registered under meson test already.

Needs environment variables (getenv) from Otium:
- tests/test_ffi.py: only compiles the fixture and passes its path; Meson could build the fixture and pass the path through the environment, then tests/ffi/common.ot runs directly.
- native/sdl/host.py + examples/run_sdl.py: honor SDL3_LIBRARY and set SDL_AUDIO_DRIVER.

Needs temporary directories / writing files from Otium:
- tests/test_library.py, tests/test_modules.py, tests/test_reader.py (module part): build throwaway module trees.
- tests/test_conditions.py: writes variant scripts around tests/conditions/common.ot.
- c/tests/test_multimethods.py: generates the 256-subset oracle program; the oracle itself could be Otium.

Needs JSON reading from Otium (or switch the data to Otium syntax):
- tools/generate-cli.py, tools/generate-reference.py (lib/catalog/*.json).
- native/sdl/host.py reads the SDL manifest; manifest could be written as Otium data.
- tests/test_reader.py reads tests/reader/datums.json.

Needs subprocesses, signals, PTYs or sockets (likely stays Python):
- tests/test_dev.py, tests/test_dev_ui.py, tests/test_cli.py, c/tests/test_cli.py, signal part of test_conditions.py, tests/test_sdl.py, tests/test_ui_gpu.py.

Deferred, no new feature needed:
- native/sdl/setup.py + schema.py -> a C probe built by Meson that prints the manifest.
- Merge benchmarks/run.py, compare.py, gc/compare.py, r7rs/run.py into one runner.
- Shared otium.test library: ~10 check files each define their own check/fails? with differing signatures.

