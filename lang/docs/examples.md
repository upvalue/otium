# Example catalog

Commands run from `lang/`. Replace `--runtime=c` with `--runtime=chez` to run
the same Otium source on Chez. Build C with `make` first.

| Example | Command | Dependencies / status |
| --- | --- | --- |
| CLI greeting | `./otium --runtime=c examples/greet.ot -- Alice` | Portable |
| Live module reload | `./otium --runtime=c -L examples examples/reload.ot` | Portable; preserves state with `defonce` |
| Multimethods | `./otium --runtime=c examples/multimethods/animals.ot` | Portable; also see `events.ot` |
| Native math | `./otium --runtime=c examples/math.ot` | `make ffi`; system math library |
| OTCL interpreter | `./otium --runtime=c -L examples/otcl examples/otcl/demo.ot` | Portable |
| SDL scenes | `tools/python examples/run_sdl.py --runtime c --scene shapes` | `make ffi sdl-setup`; SDL display/backend; also `particles` |
| UI | `./otium --runtime=c -L shared/lib -L examples examples/ui.ot` | `make ui-setup ui`; SDL GPU backend |
| Hand exercises | `./otium --runtime=c examples/hand/lc-1-two-sum.ot` | Completed examples run on both runtimes |

`examples/hand/lc-2-add-two-numbers.ot` remains unfinished and is not a passing
fixture. SDL acceptance and host sources are components of the SDL runner; its
Python bootstrap handles native ABI configuration. UI scene and callback modules
are loaded by the UI application. Native extension fixtures live with runtime
tests rather than in this catalog.
