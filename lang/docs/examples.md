# Example catalog

Commands run from `lang/`. Replace `--runtime=c` with `--runtime=chez` to run
the same Otium source on Chez. Build C with `just build` first.

| Example | Command | Dependencies / status |
| --- | --- | --- |
| CLI greeting | `./otium --runtime=c examples/greet.ot -- Alice` | Portable |
| Live module reload | `./otium --runtime=c examples/reload.ot` | Portable; preserves state with `defonce` |
| Multimethods | `./otium --runtime=c examples/multimethods/animals.ot` | Portable; also see `events.ot` |
| Native math | `./otium --runtime=c examples/math.ot` | libffi; system math library |
| OTCL interpreter | `./otium --runtime=c -L examples/otcl examples/otcl/demo.ot` | Portable |
| SDL scenes | `python3 examples/run_sdl.py --runtime c --scene shapes` | libffi, SDL3, `just sdl-manifest`; display; also `particles` |
| UI | `./otium --runtime=c examples/ui.ot` | UI build; SDL GPU backend |
| Hand exercises | `./otium --runtime=c examples/hand/lc-1-two-sum.ot` | Completed examples run on both runtimes |

`examples/hand/lc-2-add-two-numbers.ot` is unfinished. SDL acceptance and host sources are components of the SDL runner; its
Python bootstrap handles native ABI configuration. UI scene and callback modules
are loaded by the UI application. Native extension fixtures live with runtime
tests rather than in this catalog.
